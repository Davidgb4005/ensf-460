"""Build an ordered, prevalidated suite from files and directories."""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from pathlib import Path

from .models import TestSuite, TwinField
from .test_parser import parse_test_file


def load_suite(
    paths: Iterable[str | Path],
    default_limits_ms: Mapping[TwinField, int],
    *,
    name: str = "Test Suite",
) -> TestSuite:
    files = []
    for supplied_path in paths:
        path = Path(supplied_path)
        if path.is_dir():
            files.extend(sorted(path.glob("*.txt")))
        elif path.is_file():
            files.append(path)
        else:
            raise ValueError(f"test path does not exist: {path}")

    if not files:
        raise ValueError("test suite contains no test files")

    tests = tuple(parse_test_file(path, default_limits_ms) for path in files)
    return TestSuite(name=name, tests=tests)
