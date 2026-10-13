from pathlib import Path
import tempfile
import unittest

from testing.harness.models import TwinField
from testing.harness.suite_loader import load_suite
from testing.harness.test_parser import parse_duration, parse_test_file


class TestParserTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.defaults = {field_name: 100 for field_name in TwinField}

    def tearDown(self):
        self.temp_dir.cleanup()

    def write(self, name, contents):
        path = self.root / name
        path.write_text(contents, encoding="utf-8")
        return path

    def test_parses_fixed_schedule_duration_and_limit_overrides(self):
        path = self.write(
            "basic.txt",
            """
            # Fixed schedule
            name Basic countdown
            allow state 250ms
            allow elapsed 2s
            hold pb1 1s
            wait 500ms
            hold pb1+pb2 2
            """,
        )

        test = parse_test_file(path, self.defaults)

        self.assertEqual(test.name, "Basic countdown")
        self.assertEqual(test.duration_seconds, 3.5)
        self.assertEqual(test.mismatch_limits_ms[TwinField.STATE], 250)
        self.assertEqual(test.mismatch_limits_ms[TwinField.ELAPSED_SECONDS], 2000)
        self.assertTrue(test.steps[0].inputs.pb1)
        self.assertTrue(test.steps[2].inputs.pb2)

    def test_reports_file_and_line_for_invalid_commands(self):
        path = self.write("bad.txt", "name Bad\npress pb1 1s\n")
        with self.assertRaisesRegex(ValueError, r"bad\.txt:2: unknown command"):
            parse_test_file(path, self.defaults)

    def test_rejects_empty_tests_and_unknown_buttons(self):
        empty = self.write("empty.txt", "name Empty\n")
        with self.assertRaisesRegex(ValueError, "no hold or wait"):
            parse_test_file(empty, self.defaults)

        bad_button = self.write("button.txt", "hold pb4 1s\n")
        with self.assertRaisesRegex(ValueError, "unknown buttons"):
            parse_test_file(bad_button, self.defaults)

    def test_duration_defaults_match_command_context(self):
        self.assertEqual(parse_duration("10", milliseconds=True), 0.01)
        self.assertEqual(parse_duration("10"), 10.0)
        self.assertEqual(parse_duration("250ms"), 0.25)

    def test_suite_expands_directories_in_filename_order(self):
        self.write("02_second.txt", "wait 2s\n")
        self.write("01_first.txt", "wait 1s\n")
        suite = load_suite([self.root], self.defaults, name="Ordered")
        self.assertEqual(suite.name, "Ordered")
        self.assertEqual([test.name for test in suite.tests], ["01_first", "02_second"])


if __name__ == "__main__":
    unittest.main()
