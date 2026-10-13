import argparse
import sys

from testing.TUI import headless_test_runner, tui_harness_runner


def main(argv=None):
    parser = argparse.ArgumentParser(description="Countdown timer twin test runner.")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--tui", action="store_true", help="Run the live TUI harness.")
    mode.add_argument("--headless", action="store_true", help="Run command tests without the TUI.")
    parser.add_argument(
        "--manual",
        action="store_true",
        help="Run the TUI in manual mode. This is the default when no tests are given.",
    )
    parser.add_argument(
        "--tests",
        nargs="*",
        default=None,
        help="Test command files or directories to run.",
    )
    parser.add_argument("--no-startup-reset", action="store_true")
    parser.add_argument("--include-power", action="store_true")
    parser.add_argument("--power-profile", action="store_true")
    parser.add_argument("--power-port")
    parser.add_argument("--power-dir")
    parser.add_argument("--power-mode", choices=("source", "ampere"))
    parser.add_argument("--power-control-dut", action="store_true")
    args = parser.parse_args(argv)

    tests = args.tests or []
    use_headless = args.headless or (args.tests is not None and not args.tui)

    common_power_args = []
    if args.power_profile:
        common_power_args.append("--power-profile")
    if args.power_port:
        common_power_args.extend(("--power-port", args.power_port))
    if args.power_dir:
        common_power_args.extend(("--power-dir", args.power_dir))
    if args.power_mode:
        common_power_args.extend(("--power-mode", args.power_mode))
    if args.power_control_dut:
        common_power_args.append("--power-control-dut")

    if use_headless:
        headless_args = []
        if args.no_startup_reset:
            headless_args.append("--no-startup-reset")
        if args.include_power:
            headless_args.append("--include-power")
        headless_args.extend(common_power_args)
        headless_args.extend(tests)
        return headless_test_runner.main(headless_args)

    tui_args = []
    tui_args.extend(common_power_args)
    if not args.manual and tests:
        if len(tests) > 1:
            parser.error("--tui accepts at most one command file")
        tui_args.append(tests[0])
    return tui_harness_runner.main(tui_args)


if __name__ == "__main__":
    sys.exit(main())

