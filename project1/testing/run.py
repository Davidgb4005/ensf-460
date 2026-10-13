#!/usr/bin/env python3
"""Run one or more hardware-in-the-loop test files."""

from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import sys


# Make the project package importable when this file is executed directly.
PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from testing.TUI.terminal import TerminalTUI, TuiSharedState
from testing.harness import HarnessConfig, TestHarness, TestStatus, load_suite
from testing.logger import LoggerPaths, TestRigLogger
from testing.power_profiler import PowerProfilerConfig, Ppk2PowerProfiler
from testing.twin import (
    DEFAULT_CONFIG,
    DigitalTwin,
    HardwareTwin,
    HardwareTwinConfig,
    build_digital_model,
)


DEFAULT_TESTS = Path(__file__).resolve().parent / "tests"
DEFAULT_OUTPUT = Path(__file__).resolve().parent / "output"


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run countdown-timer hardware-in-the-loop tests."
    )
    parser.add_argument(
        "tests",
        nargs="*",
        type=Path,
        help=f"Test files or directories (default: {DEFAULT_TESTS})",
    )
    parser.add_argument(
        "--uart-port",
        default=DEFAULT_CONFIG.uart.port,
        help=(
            "Pi GPIO hardware UART device "
            f"(default: {DEFAULT_CONFIG.uart.port})"
        ),
    )
    parser.add_argument(
        "--baud-rate",
        type=int,
        default=DEFAULT_CONFIG.uart.baud_rate,
        help=(
            "Hardware UART baud rate "
            f"(default: {DEFAULT_CONFIG.uart.baud_rate})"
        ),
    )
    parser.add_argument(
        "--gpio-chip",
        type=int,
        default=None,
        help="GPIO chip number (default: value in testing/twin/config.py)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Parent output directory (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="Run without the terminal UI.",
    )
    parser.add_argument(
        "--no-logger",
        action="store_true",
        help="Disable the three log files.",
    )
    parser.add_argument(
        "--power-profile",
        action="store_true",
        help="Record one Nordic PPK2 current CSV per test.",
    )
    parser.add_argument("--power-port", help="PPK2 port; omit to auto-detect it.")
    parser.add_argument(
        "--power-mode",
        choices=("source", "ampere"),
        default="source",
        help="PPK2 measurement mode (default: source).",
    )
    parser.add_argument(
        "--power-voltage",
        type=int,
        default=3300,
        metavar="MV",
        help="PPK2 source or external supply voltage (default: 3300 mV).",
    )
    return parser


def run(args: argparse.Namespace) -> int:
    harness_config = HarnessConfig(hardware_uart_port=args.uart_port)
    test_paths = args.tests or [DEFAULT_TESTS]
    suite = load_suite(
        test_paths,
        harness_config.default_mismatch_limits_ms,
        name="HIL Test Suite",
    )

    session_directory = _session_directory(args.output_dir)
    logger = None if args.no_logger else _build_logger(session_directory)

    tui_state = None
    tui = None
    if not args.headless:
        tui_state = TuiSharedState()
        tui = TerminalTUI(tui_state)

    power_profiler = None
    if args.power_profile:
        power_profiler = Ppk2PowerProfiler(
            PowerProfilerConfig(
                output_directory=session_directory / "power",
                port=args.power_port,
                mode=args.power_mode,
                voltage_mv=args.power_voltage,
            )
        )

    gpio = DEFAULT_CONFIG.gpio
    gpio_chip = args.gpio_chip
    if gpio_chip is None:
        gpio_chip = gpio.gpio_chip

    digital_twin = DigitalTwin(
        build_digital_model(DEFAULT_CONFIG),
        poll_interval_seconds=DEFAULT_CONFIG.timing.twin_poll_s,
    )
    hardware_twin = HardwareTwin(
        HardwareTwinConfig(
            uart_port=args.uart_port,
            baud_rate=args.baud_rate,
            gpio_chip=gpio_chip,
            pb1_drive_pin=gpio.pb1_drive_pin,
            pb2_drive_pin=gpio.pb2_drive_pin,
            pb3_drive_pin=gpio.pb3_drive_pin,
            led1_sample_pin=gpio.led1_sample_pin,
            led2_sample_pin=gpio.led2_sample_pin,
            active_high=gpio.active_high,
            poll_interval_seconds=DEFAULT_CONFIG.timing.twin_poll_s,
        )
    )

    harness = TestHarness(
        harness_config,
        suite,
        digital_twin,
        hardware_twin,
        tui_state=tui_state,
        tui=tui,
        logger=logger,
        power_profiler=power_profiler,
    )
    result = harness.run()
    has_output_files = logger is not None or power_profiler is not None
    _print_result(result, session_directory if has_output_files else None)
    return 0 if result.status is TestStatus.PASSED else 1


def _session_directory(parent: Path) -> Path:
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    return Path(parent) / timestamp


def _build_logger(session_directory: Path) -> TestRigLogger:
    return TestRigLogger(
        LoggerPaths(
            digital_uart=session_directory / "digital-uart.log",
            hardware_uart=session_directory / "hardware-uart.log",
            state_events=session_directory / "state-events.csv",
        )
    )


def _print_result(result, output_directory: Path | None) -> None:
    print(f"Suite: {result.suite.name} - {result.status.value}")
    for test_result in result.results:
        detail = f" ({test_result.error})" if test_result.error else ""
        print(f"  {test_result.status.value:12} {test_result.test.name}{detail}")
        for failure in test_result.failures:
            print(
                f"    {failure.field.value}: mismatch for {failure.duration_ms} ms "
                f"(allowed {failure.allowed_ms} ms; "
                f"digital={failure.digital_value!r}, "
                f"hardware={failure.hardware_value!r})"
            )
    if output_directory is not None:
        print(f"Logs: {output_directory}")


def main(argv: list[str] | None = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except KeyboardInterrupt:
        print("\nTest run interrupted.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
