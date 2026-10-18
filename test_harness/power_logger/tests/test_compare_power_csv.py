import csv
import tempfile
import unittest
from pathlib import Path

from compare_power_csv import comparison_data


def write_capture(path: Path, currents):
    with path.open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["sample", "elapsed_us", "current_uA"])
        for second, current in enumerate(currents):
            writer.writerow([second, second * 1_000_000, current])


class ComparisonTests(unittest.TestCase):
    def test_trims_first_two_seconds_and_matches_shorter_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            first_path = Path(directory) / "first.csv"
            second_path = Path(directory) / "second.csv"
            write_capture(first_path, [1, 2, 3, 4, 5])
            write_capture(second_path, [10, 10, 10, 10, 10, 10])

            first, second, matched_count = comparison_data(first_path, second_path)

        self.assertEqual(matched_count, 3)
        self.assertEqual(first["times"], [0.0, 1.0, 2.0])
        self.assertEqual(second["times"], [0.0, 1.0, 2.0])
        self.assertEqual(first["average_uA"], 4.0)
        self.assertEqual(second["average_uA"], 10.0)
        self.assertAlmostEqual(first["charge_uc"], 8)
        self.assertAlmostEqual(second["charge_uc"], 20)


if __name__ == "__main__":
    unittest.main()
