import csv
import tempfile
import time
import unittest
from pathlib import Path

from ppk_tcp_server import (
    CaptureController,
    CaptureError,
    SlidingStatistics,
    capture_filename,
    extract_commands,
    parse_args,
)


class FakeSerial:
    is_open = True

    def close(self):
        self.is_open = False


class FakePPK:
    def __init__(self):
        self.ser = FakeSerial()
        self.started = 0
        self.stopped = 0
        self.power_commands = []
        self.reads = 0

    def start_measuring(self):
        self.started += 1

    def stop_measuring(self):
        self.stopped += 1

    def get_data(self):
        self.reads += 1
        return b"samples" if self.reads == 1 else b""

    def get_samples(self, _raw):
        return [10.5, 20.25], [1, 128]

    def toggle_DUT_power(self, state):
        self.power_commands.append(state)


class CommandParserTests(unittest.TestCase):
    def test_fragmented_start_command(self):
        commands, remainder = extract_commands(b"start load_")
        self.assertEqual((commands, remainder), ([], b"start load_"))
        commands, remainder = extract_commands(remainder + b"test\n")
        self.assertEqual((commands, remainder), ([('start', 'load_test')], b""))

    def test_power_commands(self):
        commands, remainder = extract_commands(b"power_on\npower_off\n")
        self.assertEqual(
            (commands, remainder),
            ([("power_on", None), ("power_off", None)], b""),
        )

    def test_invalid_command(self):
        with self.assertRaises(ValueError):
            extract_commands(b"stop\n")

    def test_filename_is_safe(self):
        self.assertEqual(capture_filename("radio load.csv"), "radio_load.csv")
        with self.assertRaises(CaptureError):
            capture_filename("../outside")

    def test_output_flags_are_independent(self):
        neither = parse_args([])
        self.assertFalse(neither.csv)
        self.assertFalse(neither.live_view)
        self.assertFalse(neither.gpio_sample)
        self.assertEqual(neither.sample, 100_000)
        self.assertEqual(neither.sample_avg, 100_000)

        both = parse_args(
            [
                "--csv",
                "--live-veiw",
                "--sample",
                "1000",
                "--sample-avg",
                "50000",
                "--gpio-sample",
            ]
        )
        self.assertTrue(both.csv)
        self.assertTrue(both.live_view)
        self.assertTrue(both.gpio_sample)
        self.assertEqual(both.sample, 1000)
        self.assertEqual(both.sample_avg, 50_000)

        corrected_spelling = parse_args(["--live-view"])
        self.assertTrue(corrected_spelling.live_view)


class CaptureTests(unittest.TestCase):
    def test_sliding_statistics_and_total_charge(self):
        statistics = SlidingStatistics(window_size=3)
        statistics.add([1.0, 5.0, 3.0, 2.0])
        average, maximum, charge_ah = statistics.snapshot()

        self.assertAlmostEqual(average, 10 / 3)
        self.assertEqual(maximum, 5.0)
        self.assertAlmostEqual(charge_ah, 11 / (1_000_000 * 100_000 * 3600))

    def test_capture_csv_and_power_safety(self):
        ppk = FakePPK()
        with tempfile.TemporaryDirectory() as directory:
            controller = CaptureController(
                ppk,
                Path(directory),
                poll_interval=0.0001,
                gpio_sample=True,
            )
            path = controller.start("capture_test")
            time.sleep(0.005)
            self.assertEqual(controller.complete(), path)

            with path.open(newline="") as csv_file:
                rows = list(csv.DictReader(csv_file))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]["current_uA"], "10.5")
            self.assertEqual(rows[0]["digital_0"], "1")
            self.assertEqual(rows[1]["digital_7"], "1")
            self.assertEqual(ppk.power_commands, [])

            controller.shutdown()
            self.assertEqual(ppk.power_commands, ["ON"])
            self.assertNotIn("OFF", ppk.power_commands)

    def test_explicit_power_control_and_shutdown_preserves_off(self):
        ppk = FakePPK()
        with tempfile.TemporaryDirectory() as directory:
            controller = CaptureController(ppk, Path(directory))
            controller.power_off()
            controller.power_on()
            controller.power_off()
            controller.shutdown()

        self.assertEqual(ppk.power_commands, ["OFF", "ON", "OFF"])

    def test_live_samples_without_csv(self):
        ppk = FakePPK()
        batches = []
        with tempfile.TemporaryDirectory() as directory:
            controller = CaptureController(
                ppk,
                Path(directory),
                poll_interval=0.0001,
                csv_enabled=False,
                sample_callback=lambda first, samples: batches.append((first, list(samples))),
            )
            self.assertIsNone(controller.start("live_test"))
            time.sleep(0.005)
            self.assertIsNone(controller.complete())
            self.assertEqual(list(Path(directory).iterdir()), [])

        self.assertEqual(batches, [(0, [10.5, 20.25])])

    def test_csv_sample_rate_downsamples_without_changing_timestamps(self):
        ppk = FakePPK()
        with tempfile.TemporaryDirectory() as directory:
            controller = CaptureController(
                ppk,
                Path(directory),
                poll_interval=0.0001,
                csv_sample_rate_hz=50_000,
            )
            path = controller.start("downsampled")
            time.sleep(0.005)
            controller.complete()

            with path.open(newline="") as csv_file:
                rows = list(csv.DictReader(csv_file))

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["sample"], "1")
        self.assertEqual(rows[0]["elapsed_us"], "10")
        self.assertNotIn("digital_bits", rows[0])

    def test_rejects_invalid_transitions(self):
        ppk = FakePPK()
        with tempfile.TemporaryDirectory() as directory:
            controller = CaptureController(ppk, Path(directory))
            with self.assertRaises(CaptureError):
                controller.complete()
            controller.start("first")
            with self.assertRaises(CaptureError):
                controller.start("second")
            controller.complete()


if __name__ == "__main__":
    unittest.main()
