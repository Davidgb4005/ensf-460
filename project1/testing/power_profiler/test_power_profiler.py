import csv
from datetime import datetime
from pathlib import Path
from tempfile import TemporaryDirectory
import time
import unittest

from testing.power_profiler import PowerProfilerConfig, Ppk2PowerProfiler


class FakeSerial:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True


class FakePpk2:
    devices = [("/dev/fake-ppk2", "SERIAL")]

    @classmethod
    def list_devices(cls):
        return cls.devices

    def __init__(self, port):
        self.port = port
        self.ser = FakeSerial()
        self.calls = []
        self.current_vdd = None
        self.data = [b"batch", b""]
        self.fail_reads = False

    def get_modifiers(self):
        self.calls.append("get_modifiers")

    def use_source_meter(self):
        self.calls.append("use_source_meter")

    def set_source_voltage(self, voltage):
        self.calls.append(("set_source_voltage", voltage))
        self.current_vdd = voltage

    def use_ampere_meter(self):
        self.calls.append("use_ampere_meter")

    def start_measuring(self):
        self.calls.append("start_measuring")

    def stop_measuring(self):
        self.calls.append("stop_measuring")

    def get_data(self):
        if self.fail_reads:
            raise OSError("serial read failed")
        return self.data.pop(0) if self.data else b""

    def get_samples(self, data):
        self.calls.append(("get_samples", data))
        return [10.25, 20.5], [0, 0]


class Factory:
    def __init__(self, api_type=FakePpk2):
        self.api_type = api_type
        self.instances = []

    def __call__(self, port):
        instance = self.api_type(port)
        self.instances.append(instance)
        return instance

    def list_devices(self):
        return self.api_type.list_devices()


class PowerProfilerTests(unittest.TestCase):
    def make_profiler(self, directory, **config_overrides):
        factory = Factory()
        config = PowerProfilerConfig(
            output_directory=Path(directory),
            poll_interval_seconds=0.001,
            **config_overrides,
        )
        profiler = Ppk2PowerProfiler(
            config,
            api_factory=factory,
            now=lambda: datetime(2026, 1, 2, 3, 4, 5, 6000),
        )
        return profiler, factory

    def test_records_current_samples_for_one_test(self):
        with TemporaryDirectory() as directory:
            profiler, factory = self.make_profiler(directory)

            path = profiler.start("button test")
            time.sleep(0.01)
            stopped_path = profiler.stop()

            self.assertEqual(path, stopped_path)
            self.assertEqual(path.name, "20260102_030405_006000_button_test_power.csv")
            with path.open(newline="", encoding="utf-8") as capture:
                rows = list(csv.reader(capture))
            self.assertEqual(rows[0], ["elapsed_seconds", "current_ua"])
            self.assertEqual(rows[1], ["0.00000000", "10.250000"])
            self.assertEqual(rows[2], ["0.00001000", "20.500000"])
            self.assertEqual(factory.instances[0].calls[:3], [
                "get_modifiers",
                "use_source_meter",
                ("set_source_voltage", 3300),
            ])
            self.assertIn("start_measuring", factory.instances[0].calls)
            self.assertIn("stop_measuring", factory.instances[0].calls)

    def test_ampere_mode_sets_the_external_supply_voltage(self):
        with TemporaryDirectory() as directory:
            profiler, factory = self.make_profiler(
                directory, mode="ampere", voltage_mv=3000
            )
            profiler.start("test")
            profiler.stop()

            device = factory.instances[0]
            self.assertIn("use_ampere_meter", device.calls)
            self.assertEqual(device.current_vdd, 3000)

    def test_auto_detect_requires_exactly_one_device(self):
        with TemporaryDirectory() as directory:
            original = FakePpk2.devices
            try:
                FakePpk2.devices = []
                profiler, _ = self.make_profiler(directory)
                with self.assertRaisesRegex(RuntimeError, "no Nordic PPK2"):
                    profiler.start("test")

                FakePpk2.devices = [("one", "1"), ("two", "2")]
                profiler, _ = self.make_profiler(directory)
                with self.assertRaisesRegex(RuntimeError, "multiple Nordic PPK2"):
                    profiler.start("test")
            finally:
                FakePpk2.devices = original

    def test_explicit_port_skips_auto_detection(self):
        with TemporaryDirectory() as directory:
            original = FakePpk2.devices
            try:
                FakePpk2.devices = []
                profiler, factory = self.make_profiler(directory, port="manual-port")
                profiler.start("test")
                profiler.stop()
                self.assertEqual(factory.instances[0].port, "manual-port")
            finally:
                FakePpk2.devices = original

    def test_worker_failure_is_reported_by_stop(self):
        with TemporaryDirectory() as directory:
            profiler, factory = self.make_profiler(directory)
            profiler.start("test")
            factory.instances[0].fail_reads = True
            time.sleep(0.01)

            with self.assertRaisesRegex(RuntimeError, "serial read failed"):
                profiler.stop()

    def test_close_is_idempotent_and_closes_serial_port(self):
        with TemporaryDirectory() as directory:
            profiler, factory = self.make_profiler(directory)
            profiler.start("test")
            profiler.close()
            serial_port = factory.instances[0].ser
            profiler.close()

            self.assertTrue(serial_port.closed)

    def test_rejects_a_second_capture_while_running(self):
        with TemporaryDirectory() as directory:
            profiler, _ = self.make_profiler(directory)
            profiler.start("one")
            try:
                with self.assertRaisesRegex(RuntimeError, "already running"):
                    profiler.start("two")
            finally:
                profiler.stop()

    def test_config_validation(self):
        with self.assertRaisesRegex(ValueError, "mode"):
            PowerProfilerConfig(Path("power"), mode="invalid")
        with self.assertRaisesRegex(ValueError, "voltage"):
            PowerProfilerConfig(Path("power"), voltage_mv=700)
        with self.assertRaisesRegex(ValueError, "poll_interval"):
            PowerProfilerConfig(Path("power"), poll_interval_seconds=0)


if __name__ == "__main__":
    unittest.main()
