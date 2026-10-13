import unittest

from testing.harness.comparator import TwinComparator
from testing.harness.models import TwinField, TwinSnapshot


def snapshot(*, state="IDLE", pb1=False):
    return TwinSnapshot(
        captured_at_ns=0,
        state=state,
        previous_state="IDLE",
        elapsed_seconds=0,
        preset_seconds=0,
        pb1=pb1,
        pb2=False,
        pb3=False,
    )


class TwinComparatorTests(unittest.TestCase):
    def test_short_mismatch_is_tolerated_and_reset(self):
        comparator = TwinComparator({TwinField.STATE: 100})
        digital = snapshot(state="RUN")
        hardware = snapshot(state="IDLE")

        self.assertEqual(comparator.update(digital, hardware, 0), ())
        self.assertEqual(comparator.update(digital, hardware, 99_000_000), ())
        self.assertEqual(comparator.update(digital, digital, 100_000_000), ())
        self.assertEqual(comparator.failures, ())

    def test_over_limit_mismatch_records_each_episode_once(self):
        comparator = TwinComparator({TwinField.PB1: 10})
        on = snapshot(pb1=True)
        off = snapshot(pb1=False)

        comparator.update(on, off, 0)
        first = comparator.update(on, off, 10_000_000)
        self.assertEqual(len(first), 1)
        self.assertEqual(first[0].field, TwinField.PB1)
        self.assertEqual(comparator.update(on, off, 20_000_000), ())

        comparator.update(off, off, 30_000_000)
        comparator.update(on, off, 40_000_000)
        second = comparator.update(on, off, 50_000_000)
        self.assertEqual(len(second), 1)
        self.assertEqual(len(comparator.failures), 2)

    def test_zero_limit_fails_on_first_observed_mismatch(self):
        comparator = TwinComparator({TwinField.STATE: 0})
        failures = comparator.update(snapshot(state="RUN"), snapshot(), 1)
        self.assertEqual(len(failures), 1)


if __name__ == "__main__":
    unittest.main()
