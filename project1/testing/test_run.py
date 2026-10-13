from pathlib import Path
import unittest

from testing.run import resolve_uart_port


class RunConfigurationTests(unittest.TestCase):
    def test_explicit_uart_port_wins(self):
        self.assertEqual(
            resolve_uart_port("/dev/custom", candidates=()),
            "/dev/custom",
        )

    def test_uart_auto_detection_uses_first_existing_candidate(self):
        existing = Path(__file__)
        self.assertEqual(
            resolve_uart_port(
                None,
                candidates=(Path("/definitely/missing"), existing),
            ),
            str(existing),
        )

    def test_uart_auto_detection_reports_missing_device(self):
        with self.assertRaisesRegex(RuntimeError, "UART is not available"):
            resolve_uart_port(None, candidates=(Path("/definitely/missing"),))


if __name__ == "__main__":
    unittest.main()
