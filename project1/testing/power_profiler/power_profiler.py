"""Small Nordic PPK2 adapter that records one CSV capture per test."""

from __future__ import annotations

import contextlib
import csv
from dataclasses import dataclass
from datetime import datetime
import io
from pathlib import Path
import re
import threading
from typing import Callable


SAMPLE_RATE_HZ = 100_000


@dataclass(frozen=True)
class PowerProfilerConfig:
    """Connection and output settings for a PPK2."""

    output_directory: Path
    port: str | None = None
    mode: str = "source"
    voltage_mv: int = 3300
    poll_interval_seconds: float = 0.01

    def __post_init__(self) -> None:
        mode = self.mode.lower()
        if mode not in {"source", "ampere"}:
            raise ValueError("mode must be 'source' or 'ampere'")
        if not 800 <= self.voltage_mv <= 5000:
            raise ValueError("voltage_mv must be between 800 and 5000")
        if self.poll_interval_seconds <= 0:
            raise ValueError("poll_interval_seconds must be greater than zero")


class Ppk2PowerProfiler:
    """Record PPK2 current samples between ``start`` and ``stop`` calls.

    The harness owns measurement timing. This class only opens/configures the
    PPK2, drains its sample stream on a worker thread, and writes the capture.
    """

    def __init__(
        self,
        config: PowerProfilerConfig,
        *,
        api_factory=None,
        now: Callable[[], datetime] = datetime.now,
    ) -> None:
        self.config = config
        self.output_path: Path | None = None
        self._api_factory = api_factory
        self._now = now
        self._ppk = None
        self._file = None
        self._writer = None
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._sample_index = 0
        self._worker_error: BaseException | None = None
        self._data_lock = threading.Lock()
        self._lifecycle_lock = threading.Lock()

    @property
    def running(self) -> bool:
        thread = self._thread
        return thread is not None and thread.is_alive()

    def start(self, label: str) -> Path:
        """Start a new current capture and return its CSV path."""
        with self._lifecycle_lock:
            if self._thread is not None:
                raise RuntimeError("power profiler capture is already running")

            self._ensure_device_open()
            path = self._make_output_path(label)
            path.parent.mkdir(parents=True, exist_ok=True)
            capture_file = path.open("w", newline="", encoding="utf-8")

            try:
                writer = csv.writer(capture_file)
                writer.writerow(("elapsed_seconds", "current_ua"))
                capture_file.flush()

                self.output_path = path
                self._file = capture_file
                self._writer = writer
                self._sample_index = 0
                self._worker_error = None
                self._stop_event.clear()
                self._ppk.start_measuring()
                self._thread = threading.Thread(
                    target=self._poll_loop,
                    name="ppk2-current-capture",
                    daemon=True,
                )
                self._thread.start()
            except BaseException:
                capture_file.close()
                self._file = None
                self._writer = None
                raise

            return path

    def stop(self) -> Path | None:
        """Stop the active capture, close its CSV, and surface worker errors."""
        with self._lifecycle_lock:
            if self._thread is None:
                return self.output_path

            self._stop_event.set()
            thread = self._thread
            thread.join(timeout=2.0)
            error = self._worker_error
            if thread.is_alive():
                error = RuntimeError("power profiler worker did not stop")

            try:
                if not thread.is_alive() and error is None:
                    self._drain_samples()
            except BaseException as exc:
                error = error or exc

            try:
                self._ppk.stop_measuring()
            except BaseException as exc:
                error = error or exc
            finally:
                if self._file is not None:
                    self._file.close()
                self._file = None
                self._writer = None
                self._thread = None

            if error is not None:
                raise RuntimeError(f"PPK2 capture failed: {error}") from error
            return self.output_path

    def close(self) -> None:
        """Stop any capture and release the PPK2 serial connection."""
        error: BaseException | None = None
        try:
            self.stop()
        except BaseException as exc:
            error = exc

        with self._lifecycle_lock:
            if self._ppk is not None:
                serial_port = getattr(self._ppk, "ser", None)
                try:
                    if serial_port is not None:
                        serial_port.close()
                finally:
                    self._ppk = None

        if error is not None:
            raise error

    def _ensure_device_open(self) -> None:
        if self._ppk is not None:
            return

        api_factory = self._api_factory
        if api_factory is None:
            try:
                from ppk2_api.ppk2_api import PPK2_API
            except ImportError as exc:
                raise RuntimeError(
                    "PPK2 support requires ppk2-api; install testing/"
                    "power_profiler/requirements.txt"
                ) from exc
            api_factory = PPK2_API

        port = self.config.port or self._detect_port(api_factory)
        ppk = api_factory(port)
        try:
            ppk.get_modifiers()
            if self.config.mode.lower() == "source":
                ppk.use_source_meter()
                ppk.set_source_voltage(self.config.voltage_mv)
            else:
                ppk.use_ampere_meter()
                # ppk2-api uses current_vdd for ampere-mode conversion but
                # does not provide a setter for the externally supplied voltage.
                ppk.current_vdd = self.config.voltage_mv
        except BaseException:
            serial_port = getattr(ppk, "ser", None)
            if serial_port is not None:
                serial_port.close()
            raise
        self._ppk = ppk

    @staticmethod
    def _detect_port(api_factory) -> str:
        devices = api_factory.list_devices()
        if not devices:
            raise RuntimeError("no Nordic PPK2 was found")
        if len(devices) > 1:
            ports = ", ".join(device[0] for device in devices)
            raise RuntimeError(f"multiple Nordic PPK2 devices found: {ports}")
        return devices[0][0]

    def _poll_loop(self) -> None:
        try:
            while not self._stop_event.is_set():
                self._drain_samples()
                self._stop_event.wait(self.config.poll_interval_seconds)
        except BaseException as exc:
            self._worker_error = exc
            self._stop_event.set()

    def _drain_samples(self) -> None:
        with self._data_lock:
            if self._writer is None or self._file is None:
                return
            data = self._ppk.get_data()
            if not data:
                return

            # The dependency prints warnings for rejected out-of-range samples.
            # The valid samples it returns should still be recorded quietly.
            with contextlib.redirect_stdout(io.StringIO()):
                samples, _digital_samples = self._ppk.get_samples(data)

            for current_ua in samples:
                elapsed_seconds = self._sample_index / SAMPLE_RATE_HZ
                self._writer.writerow((f"{elapsed_seconds:.8f}", f"{current_ua:.6f}"))
                self._sample_index += 1
            self._file.flush()

    def _make_output_path(self, label: str) -> Path:
        safe_label = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(label)).strip("_")
        if not safe_label:
            safe_label = "test"
        timestamp = self._now().strftime("%Y%m%d_%H%M%S_%f")
        return Path(self.config.output_directory) / f"{timestamp}_{safe_label}_power.csv"
