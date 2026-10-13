import csv
from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum
import json
from pathlib import Path
import tempfile
import unittest

from testing.logger import LoggerPaths, TestRigLogger


FIXED_TIME = datetime(2026, 10, 12, 20, 15, 30, 123000, tzinfo=timezone.utc)


class State(Enum):
    RUN = 1


@dataclass
class Button:
    pressed: bool


class TestRigLoggerTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        root = Path(self.temp_dir.name)
        self.paths = LoggerPaths(
            digital_uart=root / "uart" / "digital.log",
            hardware_uart=root / "uart" / "hardware.log",
            state_events=root / "events" / "state.csv",
        )
        self.logger = TestRigLogger(self.paths, clock=lambda: FIXED_TIME)
        self.digital = {"state": State.RUN, "elapsed": 3, "pb1": Button(False)}
        self.hardware = {"state": "RUN", "elapsed": 3, "pb1": "OFF"}

    def tearDown(self):
        self.temp_dir.cleanup()

    def read_events(self):
        with Path(self.paths.state_events).open(newline="", encoding="utf-8") as log:
            return list(csv.DictReader(log))

    def test_initialization_creates_exactly_three_output_files(self):
        self.assertTrue(Path(self.paths.digital_uart).is_file())
        self.assertTrue(Path(self.paths.hardware_uart).is_file())
        self.assertTrue(Path(self.paths.state_events).is_file())
        self.assertEqual(self.read_events(), [])

    def test_state_change_writes_only_full_state_log(self):
        self.logger.log_state_change(
            "STATE: IDLE->RUN", self.digital, self.hardware
        )

        self.assertEqual(Path(self.paths.digital_uart).read_text(), "")
        self.assertEqual(Path(self.paths.hardware_uart).read_text(), "")
        rows = self.read_events()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["timestamp_utc"], "2026-10-12T20:15:30.123Z")
        self.assertEqual(rows[0]["action"], "STATE: IDLE->RUN")
        self.assertEqual(rows[0]["uart_source"], "")
        self.assertEqual(rows[0]["uart_message"], "")
        self.assertEqual(json.loads(rows[0]["digital_state"])["state"], "RUN")
        self.assertEqual(json.loads(rows[0]["digital_state"])["pb1"], {"pressed": False})
        self.assertEqual(json.loads(rows[0]["hardware_state"]), self.hardware)

    def test_digital_uart_writes_digital_file_and_full_state_log(self):
        message = "[00:00:03.120] State: RUN"
        self.logger.log_uart("digital", message, self.digital, self.hardware)

        self.assertEqual(Path(self.paths.digital_uart).read_text(), message + "\n")
        self.assertEqual(Path(self.paths.hardware_uart).read_text(), "")
        row = self.read_events()[0]
        self.assertEqual(row["action"], "DIGITAL_UART_MESSAGE")
        self.assertEqual(row["uart_source"], "digital")
        self.assertEqual(row["uart_message"], message)

    def test_hardware_uart_preserves_existing_line_ending(self):
        message = "[00:00:03.125] State: RUN\n"
        self.logger.log_uart("HARDWARE", message, self.digital, self.hardware)

        self.assertEqual(Path(self.paths.hardware_uart).read_text(), message)
        self.assertEqual(self.read_events()[0]["uart_message"], message)

    def test_reopening_logger_does_not_duplicate_csv_header(self):
        self.logger.log_state_change("PB1: OFF->ON", self.digital, self.hardware)
        reopened = TestRigLogger(self.paths, clock=lambda: FIXED_TIME)
        reopened.log_state_change("PB1: ON->OFF", self.digital, self.hardware)

        self.assertEqual(len(self.read_events()), 2)

    def test_invalid_inputs_fail_before_writing(self):
        with self.assertRaises(ValueError):
            self.logger.log_uart("unknown", "message", self.digital, self.hardware)
        with self.assertRaises(TypeError):
            self.logger.log_uart("digital", 12, self.digital, self.hardware)
        with self.assertRaises(ValueError):
            self.logger.log_state_change("", self.digital, self.hardware)
        with self.assertRaises(TypeError):
            self.logger.log_state_change("STATE", object(), self.hardware)
        self.assertEqual(self.read_events(), [])


if __name__ == "__main__":
    unittest.main()
