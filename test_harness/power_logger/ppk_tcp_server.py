#!/usr/bin/env python3
"""TCP-controlled CSV recorder for a Nordic Power Profiler Kit II."""

from __future__ import annotations

import argparse
import csv
import logging
import queue
import re
import signal
import socketserver
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any, Callable, Optional, Sequence, Tuple

try:
    from ppk2_api.ppk2_api import PPK2_API
except ImportError:  # Allows the command parser and recorder to be tested without hardware deps.
    PPK2_API = None  # type: ignore[assignment,misc]


LOG = logging.getLogger("ppk-tcp-server")
SAMPLE_RATE_HZ = 100_000
SAMPLE_PERIOD_NS = 1_000_000_000 // SAMPLE_RATE_HZ
DRAIN_IDLE_SECONDS = 0.02
LIVE_DECIMATION = 100
LIVE_WINDOW_SECONDS = 20 
CSV_BASE_HEADER = [
    "sample",
    "timestamp_ns_utc",
    "elapsed_us",
    "current_uA",
]
CSV_GPIO_HEADER = [
    "digital_bits",
    *(f"digital_{channel}" for channel in range(8)),
]


if PPK2_API is not None:
    class PowerSafePPK2(PPK2_API):  # type: ignore[misc,valid-type]
        """Prevent the upstream destructor from resetting (and depowering) the PPK2."""

        def __del__(self) -> None:
            try:
                serial_port = getattr(self, "ser", None)
                if serial_port is not None and serial_port.is_open:
                    serial_port.close()
            except Exception:
                pass
else:
    PowerSafePPK2 = None  # type: ignore[misc,assignment]


class CaptureError(RuntimeError):
    pass


def capture_filename(test_name: str) -> str:
    """Return a safe CSV filename without allowing paths or silent overwrites."""
    name = test_name.strip()
    if not name:
        raise CaptureError("start requires a test filename")
    if "/" in name or "\\" in name or name in {".", ".."}:
        raise CaptureError("test filename must not contain a path")
    if name.lower().endswith(".csv"):
        name = name[:-4]
    name = re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("._")
    if not name:
        raise CaptureError("test filename contains no usable characters")
    return f"{name}.csv"


def csv_sample_rate(value: str) -> int:
    """Validate the requested number of CSV rows retained per second."""
    try:
        rate = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("sample rate must be an integer") from error
    if not 1 <= rate <= SAMPLE_RATE_HZ:
        raise argparse.ArgumentTypeError(
            f"sample rate must be between 1 and {SAMPLE_RATE_HZ}"
        )
    return rate


def positive_int(value: str) -> int:
    try:
        number = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("value must be an integer") from error
    if number < 1:
        raise argparse.ArgumentTypeError("value must be at least 1")
    return number


class SlidingStatistics:
    """Efficient sliding average/max plus charge integration at 100 kS/s."""

    def __init__(self, window_size: int) -> None:
        self.window_size = window_size
        self.reset()

    def reset(self) -> None:
        self._window: deque[Tuple[int, float]] = deque()
        self._maximums: deque[Tuple[int, float]] = deque()
        self._window_sum = 0.0
        self._sequence = 0
        self._charge_ah = 0.0

    def add(self, currents: Sequence[float]) -> None:
        for raw_current in currents:
            current = float(raw_current)
            sequence = self._sequence
            self._sequence += 1
            self._window.append((sequence, current))
            self._window_sum += current
            while self._maximums and self._maximums[-1][1] <= current:
                self._maximums.pop()
            self._maximums.append((sequence, current))

            if len(self._window) > self.window_size:
                old_sequence, old_current = self._window.popleft()
                self._window_sum -= old_current
                if self._maximums and self._maximums[0][0] == old_sequence:
                    self._maximums.popleft()

            # current is in uA and the fixed PPK2 sampling period is 1/100000 s.
            self._charge_ah += current / (1_000_000 * SAMPLE_RATE_HZ * 3600)

    def snapshot(self) -> Tuple[float, float, float]:
        average = self._window_sum / len(self._window) if self._window else 0.0
        maximum = self._maximums[0][1] if self._maximums else 0.0
        return average, maximum, self._charge_ah


class LivePlot:
    """Thread-safe, decimated live current plot driven by matplotlib animation."""

    def __init__(self, average_window_samples: int) -> None:
        try:
            import matplotlib.animation as animation
            import matplotlib.pyplot as plt
        except ImportError as error:
            raise RuntimeError(
                "matplotlib is required for --live-veiw; run: pip install -r requirements.txt"
            ) from error

        self._plt = plt
        self._statistics = SlidingStatistics(average_window_samples)
        self._statistics_lock = threading.Lock()
        self._pending: queue.SimpleQueue[Tuple[list[float], list[float]]] = queue.SimpleQueue()
        displayed_rate = SAMPLE_RATE_HZ // LIVE_DECIMATION
        self._times: deque[float] = deque(maxlen=displayed_rate * LIVE_WINDOW_SECONDS)
        self._currents: deque[float] = deque(maxlen=displayed_rate * LIVE_WINDOW_SECONDS)
        self._figure, self._axes = plt.subplots()
        (self._line,) = self._axes.plot([], [], linewidth=1)
        self._axes.set_title("PPK2 live current")
        self._axes.set_xlabel("Capture time (s)")
        self._axes.set_ylabel("Current (uA)")
        self._axes.grid(True)
        self._statistics_text = self._axes.text(
            0.02,
            0.98,
            "",
            transform=self._axes.transAxes,
            va="top",
            ha="left",
            bbox={"boxstyle": "round", "facecolor": "white", "alpha": 0.8},
        )
        self._animation = animation.FuncAnimation(
            self._figure,
            self._update,
            interval=100,
            cache_frame_data=False,
        )

    def add_samples(self, first_sample: int, currents: Sequence[float]) -> None:
        if first_sample == 0:
            self._pending.put(([], []))
            with self._statistics_lock:
                self._statistics.reset()
        with self._statistics_lock:
            self._statistics.add(currents)
        indexes = range(0, len(currents), LIVE_DECIMATION)
        times = [(first_sample + index) / SAMPLE_RATE_HZ for index in indexes]
        values = [float(currents[index]) for index in range(0, len(currents), LIVE_DECIMATION)]
        if values:
            self._pending.put((times, values))

    def _update(self, _frame: Any) -> Tuple[Any, ...]:
        changed = False
        while True:
            try:
                times, currents = self._pending.get_nowait()
            except queue.Empty:
                break
            if not times and not currents:
                self._times.clear()
                self._currents.clear()
                changed = True
                continue
            self._times.extend(times)
            self._currents.extend(currents)
            changed = True

        if changed:
            self._line.set_data(self._times, self._currents)
            if self._times:
                right = self._times[-1]
                self._axes.set_xlim(max(0.0, right - LIVE_WINDOW_SECONDS), max(1.0, right))
            self._axes.relim()
            self._axes.autoscale_view(scalex=False, scaley=True)
        with self._statistics_lock:
            average, maximum, charge_ah = self._statistics.snapshot()
        self._statistics_text.set_text(
            f"Window avg: {average:.3f} uA\n"
            f"Window max: {maximum:.3f} uA\n"
            f"Total charge: {charge_ah:.9f} Ah"
        )
        return self._line, self._statistics_text

    def show(self) -> None:
        self._plt.show()

    def close(self) -> None:
        self._plt.close(self._figure)


class CaptureController:
    """Owns the PPK2 and serializes capture state transitions."""

    def __init__(
        self,
        ppk: Any,
        output_dir: Path,
        poll_interval: float = 0.001,
        csv_enabled: bool = True,
        csv_sample_rate_hz: int = SAMPLE_RATE_HZ,
        gpio_sample: bool = False,
        sample_callback: Optional[Callable[[int, Sequence[float]], None]] = None,
    ):
        self._ppk = ppk
        self._output_dir = output_dir
        self._poll_interval = poll_interval
        self._csv_enabled = csv_enabled
        self._csv_sample_rate_hz = csv_sample_rate_hz
        self._gpio_sample = gpio_sample
        self._sample_callback = sample_callback
        self._lock = threading.Lock()
        self._io_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._active_path: Optional[Path] = None
        self._last_path: Optional[Path] = None
        self._worker_error: Optional[BaseException] = None
        self._power_on = True

    @property
    def active_path(self) -> Optional[Path]:
        with self._lock:
            return self._active_path

    @property
    def is_recording(self) -> bool:
        with self._lock:
            return self._thread is not None

    def start(self, test_name: str) -> Optional[Path]:
        with self._lock:
            if self._thread is not None:
                raise CaptureError("a recording is already active")

            filename = capture_filename(test_name)
            path = None
            if self._csv_enabled:
                self._output_dir.mkdir(parents=True, exist_ok=True)
                path = self._output_dir / filename
                if path.exists():
                    raise CaptureError(f"capture already exists: {path}")
            self._stop_event.clear()
            self._worker_error = None
            self._active_path = path

            # Starting/stopping measurement is deliberately separate from DUT power.
            with self._io_lock:
                self._ppk.start_measuring()
            self._thread = threading.Thread(
                target=self._record,
                args=(path,),
                name="ppk-csv-recorder",
                daemon=False,
            )
            self._thread.start()
            return path

    def complete(self) -> Optional[Path]:
        with self._lock:
            thread = self._thread
            path = self._active_path
            if thread is None:
                raise CaptureError("no recording is active")
            self._stop_event.set()

        thread.join()

        with self._lock:
            error = self._worker_error
            self._thread = None
            self._active_path = None
            self._last_path = path

        if error is not None:
            raise CaptureError(f"recording failed: {error}") from error
        return path

    def power_on(self) -> None:
        """Explicitly enable the PPK2 DUT power output."""
        with self._io_lock:
            self._ppk.toggle_DUT_power("ON")
            self._power_on = True

    def power_off(self) -> None:
        """Explicitly disable the PPK2 DUT power output."""
        with self._io_lock:
            self._ppk.toggle_DUT_power("OFF")
            self._power_on = False

    def shutdown(self) -> None:
        """Stop any capture and close serial while intentionally retaining DUT power."""
        if self.is_recording:
            try:
                self.complete()
            except CaptureError:
                LOG.exception("Could not cleanly finish the active recording")

        try:
            # Preserve an explicit power_off across shutdown. Otherwise reassert ON.
            if self._power_on:
                with self._io_lock:
                    self._ppk.toggle_DUT_power("ON")
        finally:
            serial_port = getattr(self._ppk, "ser", None)
            if serial_port is not None and getattr(serial_port, "is_open", False):
                serial_port.close()

    def _record(self, path: Optional[Path]) -> None:
        start_ns = time.time_ns()
        sample_number = 0
        csv_file = None
        try:
            writer = None
            if path is not None:
                csv_file = path.open("x", newline="", buffering=1024 * 1024)
                writer = csv.writer(csv_file)
                header = CSV_BASE_HEADER + (CSV_GPIO_HEADER if self._gpio_sample else [])
                writer.writerow(header)

            while not self._stop_event.is_set():
                sample_number = self._read_and_write(writer, start_ns, sample_number)
                time.sleep(self._poll_interval)

            with self._io_lock:
                self._ppk.stop_measuring()

            # The stop command and samples travel in opposite USB directions. Keep
            # draining until the receive side has remained idle long enough for the
            # final in-flight samples to arrive.
            drain_until = time.monotonic() + DRAIN_IDLE_SECONDS
            while time.monotonic() < drain_until:
                with self._io_lock:
                    raw = self._ppk.get_data()
                if not raw:
                    time.sleep(self._poll_interval)
                    continue
                sample_number = self._write_raw(writer, raw, start_ns, sample_number)
                drain_until = time.monotonic() + DRAIN_IDLE_SECONDS
        except BaseException as error:
            self._worker_error = error
            try:
                with self._io_lock:
                    self._ppk.stop_measuring()
            except Exception:
                LOG.exception("Could not stop measurement after recorder failure")
        finally:
            if csv_file is not None:
                csv_file.close()

    def _read_and_write(
        self, writer: Optional[Any], start_ns: int, sample_number: int
    ) -> int:
        with self._io_lock:
            raw = self._ppk.get_data()
            if not raw:
                return sample_number
            return self._write_raw(writer, raw, start_ns, sample_number)

    def _write_raw(
        self, writer: Optional[Any], raw: bytes, start_ns: int, sample_number: int
    ) -> int:
        currents, digital_values = self._ppk.get_samples(raw)
        first_sample = sample_number
        for index, current in enumerate(currents):
            timestamp_ns = start_ns + sample_number * SAMPLE_PERIOD_NS
            # The PPK2 always produces 100 kS/s. Select exactly the requested
            # average number of CSV rows per second without changing acquisition.
            keep_csv_sample = (
                (sample_number + 1) * self._csv_sample_rate_hz // SAMPLE_RATE_HZ
                > sample_number * self._csv_sample_rate_hz // SAMPLE_RATE_HZ
            )
            if writer is not None and keep_csv_sample:
                row = [
                    sample_number,
                    timestamp_ns,
                    sample_number * SAMPLE_PERIOD_NS // 1_000,
                    current,
                ]
                if self._gpio_sample:
                    if index < len(digital_values):
                        bits = int(digital_values[index])
                        row.extend(
                            [bits, *((bits >> channel) & 1 for channel in range(8))]
                        )
                    else:
                        row.extend([""] * len(CSV_GPIO_HEADER))
                writer.writerow(row)
            sample_number += 1
        written_count = sample_number - first_sample
        if self._sample_callback is not None and written_count:
            self._sample_callback(first_sample, currents[:written_count])
        return sample_number


def extract_commands(buffer: bytes) -> Tuple[list[Tuple[str, Optional[str]]], bytes]:
    """Extract newline-terminated commands and retain any partial final line."""
    commands: list[Tuple[str, Optional[str]]] = []
    remaining = buffer

    while b"\n" in remaining:
        raw_line, remaining = remaining.split(b"\n", 1)
        raw_line = raw_line.rstrip(b"\r").strip()
        if not raw_line:
            continue
        try:
            line = raw_line.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValueError("commands and filenames must be UTF-8") from error

        command, separator, argument = line.partition(" ")
        command = command.lower()
        if command == "start":
            test_name = argument.strip() if separator else ""
            if not test_name:
                raise ValueError("start requires a test filename")
            commands.append((command, test_name))
        elif command in {"complete", "power_on", "power_off"}:
            if separator and argument.strip():
                raise ValueError(f"{command} does not accept an argument")
            commands.append((command, None))
        else:
            raise ValueError(
                "expected 'start <test_filename>', 'complete', 'power_on', or 'power_off'"
            )

    return commands, remaining


class CommandHandler(socketserver.BaseRequestHandler):
    controller: CaptureController

    def handle(self) -> None:
        buffer = b""
        while True:
            chunk = self.request.recv(1024)
            if not chunk:
                if buffer.strip():
                    LOG.warning(
                        "Discarded incomplete command from %s (missing newline)",
                        self.client_address,
                    )
                return
            buffer += chunk
            try:
                commands, buffer = extract_commands(buffer)
            except ValueError as error:
                LOG.warning("Rejected command from %s: %s", self.client_address, error)
                buffer = b""
                continue

            for command, argument in commands:
                self._execute(command, argument)

    def _execute(self, command: str, argument: Optional[str]) -> None:
        try:
            if command == "start":
                path = self.controller.start(argument or "")
                if path is None:
                    LOG.info("Recording started for %s (CSV output disabled)", argument)
                else:
                    LOG.info("Recording started: %s", path)
            elif command == "complete":
                path = self.controller.complete()
                if path is None:
                    LOG.info("Recording complete")
                else:
                    LOG.info("Recording complete: %s", path)
            elif command == "power_on":
                self.controller.power_on()
                LOG.info("DUT power ON")
            else:
                self.controller.power_off()
                LOG.info("DUT power OFF")
        except Exception as error:
            LOG.error("Command %s failed: %s", command, error)


class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def create_ppk(port: Optional[str], voltage_mv: int) -> Any:
    if PowerSafePPK2 is None or PPK2_API is None:
        raise RuntimeError("ppk2-api is not installed; run: pip install -r requirements.txt")

    selected_port = port
    if selected_port is None:
        devices: Sequence[Tuple[str, str]] = PPK2_API.list_devices()
        if len(devices) != 1:
            found = ", ".join(device[0] for device in devices) or "none"
            raise RuntimeError(f"expected exactly one PPK2 (found: {found}); pass --port")
        selected_port = devices[0][0]

    ppk = PowerSafePPK2(selected_port)
    ppk.get_modifiers()
    ppk.use_ampere_meter()
    # In ampere-meter mode this value represents the external input voltage used
    # by the API's current conversion; the PPK2 does not generate or regulate it.
    ppk.set_source_voltage(voltage_mv)
    ppk.toggle_DUT_power("ON")
    return ppk


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="PPK2 serial port; auto-detected when omitted")
    parser.add_argument(
        "--voltage-mv",
        type=int,
        default=3300,
        help="external DUT supply voltage in mV, used for PPK2 calibration",
    )
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--tcp-port", type=int, default=5000)
    parser.add_argument("--output-dir", type=Path, default=Path("captures"))
    parser.add_argument(
        "--live-veiw",
        "--live-view",
        dest="live_view",
        action="store_true",
        help="show an animated live current plot (both spellings are accepted)",
    )
    parser.add_argument(
        "--csv",
        action="store_true",
        help="write samples to the named CSV in --output-dir",
    )
    parser.add_argument(
        "--sample",
        type=csv_sample_rate,
        default=SAMPLE_RATE_HZ,
        metavar="RATE",
        help="CSV rows per second, from 1 to 100000 (default: 100000)",
    )
    parser.add_argument(
        "--gpio-sample",
        action="store_true",
        help="include the raw GPIO bitmask and digital channels 0-7 in the CSV",
    )
    parser.add_argument(
        "--sample-avg",
        type=positive_int,
        default=SAMPLE_RATE_HZ,
        metavar="SAMPLES",
        help="raw-sample window for live average/max text (default: 100000 = 1 s)",
    )
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    ppk = create_ppk(args.port, args.voltage_mv)
    live_plot = LivePlot(args.sample_avg) if args.live_view else None
    controller = CaptureController(
        ppk,
        args.output_dir,
        csv_enabled=args.csv,
        csv_sample_rate_hz=args.sample,
        gpio_sample=args.gpio_sample,
        sample_callback=live_plot.add_samples if live_plot is not None else None,
    )
    CommandHandler.controller = controller

    with ThreadedTCPServer((args.host, args.tcp_port), CommandHandler) as server:
        stop_once = threading.Event()

        def request_shutdown(_signum: int, _frame: Any) -> None:
            if not stop_once.is_set():
                stop_once.set()
                threading.Thread(target=server.shutdown, daemon=True).start()
                if live_plot is not None:
                    live_plot.close()

        signal.signal(signal.SIGINT, request_shutdown)
        signal.signal(signal.SIGTERM, request_shutdown)
        LOG.info(
            "Ampere Meter mode; external supply %d mV; DUT path ON; listening on %s:%d",
            args.voltage_mv,
            args.host,
            args.tcp_port,
        )
        if args.csv:
            LOG.info(
                "CSV output enabled at %d rows/second; GPIO sampling %s",
                args.sample,
                "enabled" if args.gpio_sample else "disabled",
            )
        try:
            if live_plot is None:
                server.serve_forever()
            else:
                server_thread = threading.Thread(
                    target=server.serve_forever,
                    name="ppk-tcp-server",
                    daemon=True,
                )
                server_thread.start()
                try:
                    live_plot.show()
                finally:
                    server.shutdown()
                    server_thread.join()
        finally:
            controller.shutdown()
            LOG.info("Server stopped; DUT power state was preserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
