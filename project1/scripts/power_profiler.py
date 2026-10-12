import csv
import os
import re
import threading
import time
from datetime import datetime
from pathlib import Path


class NoPowerProfiler:
    enabled = False
    output_path = None

    def start(self, label):
        return None

    def stop(self):
        return None


class Ppk2PowerProfiler:
    enabled = True

    def __init__(self, config):
        self.config = config
        self.output_path = None
        self._ppk = None
        self._thread = None
        self._stop_event = threading.Event()
        self._lock = threading.Lock()
        self._sample_index = 0
        self._csv_file = None
        self._writer = None

    def start(self, label):
        try:
            from ppk2_api.ppk2_api import PPK2_API
        except ImportError as exc:
            raise RuntimeError(
                "PPK2 power profiling needs the ppk2_api Python package. "
                "Install it before enabling power profiling."
            ) from exc

        port = self.config.power.port or self._auto_detect_port(PPK2_API)
        self._check_port_access(port)

        self._ppk = PPK2_API(port)
        self._ppk.get_modifiers()
        self._configure_mode()

        self.output_path = self._output_path(label)
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._csv_file = open(self.output_path, "w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._csv_file)
        self._writer.writerow(
            (
                "elapsed_s",
                "sample_index",
                "current_ua",
                "digital_bits",
            )
        )

        self._stop_event.clear()
        self._sample_index = 0
        self._ppk.start_measuring()

        if self.config.power.mode.lower() == "source" and self.config.power.enable_dut_power:
            self._ppk.toggle_DUT_power("ON")

        self._thread = threading.Thread(target=self._poll_loop, daemon=True)
        self._thread.start()
        return self.output_path

    def stop(self):
        if self._ppk is None:
            return self.output_path

        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

        self._drain_samples()

        with self._lock:
            if self.config.power.mode.lower() == "source" and self.config.power.enable_dut_power:
                self._ppk.toggle_DUT_power("OFF")
            self._ppk.stop_measuring()

            if self._csv_file is not None:
                self._csv_file.close()

            self._thread = None
            self._csv_file = None
            self._writer = None
            self._ppk = None
        return self.output_path

    def _auto_detect_port(self, ppk2_api):
        devices = ppk2_api.list_devices()
        if not devices:
            raise RuntimeError(
                "No PPK2 serial device found. Set CONFIG.power.port or pass --power-port."
            )
        if len(devices) > 1:
            ports = ", ".join(device for device, _serial in devices)
            raise RuntimeError(f"Multiple PPK2 devices found ({ports}); set --power-port.")
        return devices[0][0]

    def _check_port_access(self, port):
        if not os.path.exists(port):
            raise RuntimeError(f"PPK2 serial port {port} does not exist.")
        if not os.access(port, os.R_OK | os.W_OK):
            raise RuntimeError(
                f"Permission denied opening PPK2 serial port {port}. "
                "Add your user to the port's group, reload udev, then log out/in "
                "or run the test with sudo."
            )

    def _configure_mode(self):
        mode = self.config.power.mode.lower()
        if mode == "source":
            self._ppk.use_source_meter()
            self._ppk.set_source_voltage(self.config.power.source_voltage_mv)
            return

        if mode == "ampere":
            self._ppk.use_ampere_meter()
            self._ppk.current_vdd = self.config.power.source_voltage_mv
            return

        raise RuntimeError(f"unknown PPK2 mode {self.config.power.mode!r}")

    def _poll_loop(self):
        while not self._stop_event.is_set():
            self._drain_samples()
            time.sleep(self.config.power.poll_interval_s)

    def _drain_samples(self):
        with self._lock:
            if self._ppk is None or self._writer is None or self._csv_file is None:
                return

            data = self._ppk.get_data()
            if not data:
                return

            samples, digital_bits = self._ppk.get_samples(data)
            for offset, current_ua in enumerate(samples):
                bits = digital_bits[offset] if offset < len(digital_bits) else ""
                elapsed_s = self._sample_index / self.config.power.sample_rate_hz
                self._writer.writerow(
                    (
                        f"{elapsed_s:.8f}",
                        self._sample_index,
                        f"{current_ua:.6f}",
                        bits,
                    )
                )
                self._sample_index += 1

            self._csv_file.flush()

    def _output_path(self, label):
        safe_label = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(label)).strip("_")
        if not safe_label:
            safe_label = "power_capture"
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        return Path(self.config.power.output_dir) / f"{timestamp}_{safe_label}.csv"


def build_power_profiler(config):
    if not config.power.enabled:
        return NoPowerProfiler()
    return Ppk2PowerProfiler(config)
