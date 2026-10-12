import argparse
import copy
import subprocess
import sys
import time
from pathlib import Path

from command_input import load_commands, run_commands
from digital_twin import build_digital_twin
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


def _new_digital_twin():
    config = copy.deepcopy(CONFIG)
    config.mode = TestMode.AUTOMATED
    digital_twin = build_digital_twin(config)
    digital_twin.main()
    time.sleep(0.05)
    return digital_twin


def run_file(path):
    digital_twin = _new_digital_twin()
    commands = load_commands(path)
    run_commands(commands, digital_twin)


def main():
    parser = argparse.ArgumentParser(description="Run digital twin command tests.")
    parser.add_argument("paths", nargs="*", help="Test files or directories to run.")
    parser.add_argument("--worker", help=argparse.SUPPRESS)
    parser.add_argument(
        "--include-power",
        action="store_true",
        help="Also run long power-consumption command scenarios.",
    )
    args = parser.parse_args()

    if args.worker:
        run_file(Path(args.worker))
        return 0

    failures = []
    test_files = list(_iter_test_files(args.paths, args.include_power))
    if not test_files:
        print("No test files found.")
        return 1

    for path in test_files:
        result = subprocess.run(
            [sys.executable, __file__, "--worker", str(path)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            message = (result.stderr or result.stdout).strip().splitlines()
            detail = message[-1] if message else f"exit code {result.returncode}"
            failures.append((path, detail))
            print(f"FAIL {path}: {detail}")
        else:
            print(f"PASS {path}")

    print(f"\n{len(test_files) - len(failures)} passed, {len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
