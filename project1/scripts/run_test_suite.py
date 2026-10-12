import argparse
import copy
import subprocess
import sys
import time
from pathlib import Path

from command_input import load_commands, run_commands
from digital_twin import build_digital_twin
from power_profiler import build_power_profiler
from testing_config import CONFIG, TestMode


TEST_ROOT = Path(__file__).with_name("test_suites")
DEFAULT_GROUPS = ("small", "large_functionality")


def _iter_test_files(paths, include_power):
    selected = []
    if paths:
        for path in paths:
            selected.append(Path(path))
    else:
        for group in DEFAULT_GROUPS:
            selected.append(TEST_ROOT / group)
        if include_power:
            selected.append(TEST_ROOT / "large_power")

    for path in selected:
        if path.is_dir():
            yield from sorted(path.glob("*.txt"))
        else:
            yield path


def _new_config(args):
    config = copy.deepcopy(CONFIG)
    config.mode = TestMode.AUTOMATED
    if args.power_profile:
        config.power.enabled = True
    if args.power_port is not None:
        config.power.port = args.power_port
    if args.power_dir is not None:
        config.power.output_dir = args.power_dir
    if args.power_mode is not None:
        config.power.mode = args.power_mode
    return config


def _new_digital_twin(config):
    digital_twin = build_digital_twin(config)
    digital_twin.main()
    time.sleep(0.05)
    return digital_twin


def run_file(path, args):
    config = _new_config(args)
    digital_twin = _new_digital_twin(config)
    commands = load_commands(path)
    run_commands(commands, digital_twin, startup_reset=not args.no_startup_reset)


def main():
    parser = argparse.ArgumentParser(description="Run digital twin command tests.")
    parser.add_argument("paths", nargs="*", help="Test files or directories to run.")
    parser.add_argument("--worker", help=argparse.SUPPRESS)
    parser.add_argument(
        "--no-startup-reset",
        action="store_true",
        help="Skip the default PB3 clear and PB1+PB2 reset preamble.",
    )
    parser.add_argument(
        "--include-power",
        action="store_true",
        help="Also run long power-consumption command scenarios.",
    )
    parser.add_argument(
        "--power-profile",
        action="store_true",
        help="Record a PPK2 CSV capture for each test file.",
    )
    parser.add_argument("--power-port", help="PPK2 serial port, for example /dev/ttyACM0.")
    parser.add_argument("--power-dir", help="Directory for PPK2 CSV captures.")
    parser.add_argument(
        "--power-mode",
        choices=("source", "ampere"),
        help="PPK2 mode. Defaults to CONFIG.power.mode.",
    )
    args = parser.parse_args()

    if args.worker:
        run_file(Path(args.worker), args)
        return 0

    failures = []
    test_files = list(_iter_test_files(args.paths, args.include_power))
    if not test_files:
        print("No test files found.")
        return 1

    power_profiler = build_power_profiler(_new_config(args))
    try:
        for path in test_files:
            try:
                power_path = power_profiler.start(Path(path).stem)
            except Exception as exc:
                failures.append((path, exc))
                print(f"FAIL {path}: power profiler start failed: {exc}")
                continue

            result = subprocess.run(
                [
                    sys.executable,
                    __file__,
                    "--worker",
                    str(path),
                    *(
                        ("--no-startup-reset",)
                        if args.no_startup_reset
                        else ()
                    ),
                ],
                capture_output=True,
                text=True,
            )

            try:
                power_profiler.stop()
            except Exception as exc:
                failures.append((path, exc))
                print(f"FAIL {path}: power profiler stop failed: {exc}")
                continue

            if result.returncode != 0:
                message = (result.stderr or result.stdout).strip().splitlines()
                detail = message[-1] if message else f"exit code {result.returncode}"
                failures.append((path, detail))
                print(f"FAIL {path}: {detail}")
            else:
                suffix = f" power={power_path}" if power_path is not None else ""
                print(f"PASS {path}{suffix}")
    finally:
        power_profiler.close()

    print(f"\n{len(test_files) - len(failures)} passed, {len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
