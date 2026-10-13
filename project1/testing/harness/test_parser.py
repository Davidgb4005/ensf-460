"""Parser for fixed-duration harness test files."""

from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path
import re

from .models import ButtonInputs, HoldStep, TestCase, TwinField, WaitStep


_DURATION = re.compile(r"^(?P<value>(?:\d+(?:\.\d*)?|\.\d+))(?P<unit>ms|s)?$")
_FIELD_ALIASES = {
    "elapsed": TwinField.ELAPSED_SECONDS,
    "preset": TwinField.PRESET_SECONDS,
}


def parse_duration(value: str, *, milliseconds: bool = False) -> float:
    match = _DURATION.fullmatch(value.lower())
    if match is None:
        raise ValueError(f"invalid duration {value!r}")
    amount = float(match.group("value"))
    unit = match.group("unit") or ("ms" if milliseconds else "s")
    return amount / 1000 if unit == "ms" else amount


def parse_test_file(
    path: str | Path,
    default_limits_ms: Mapping[TwinField, int],
) -> TestCase:
    test_path = Path(path)
    limits = _validated_limits(default_limits_ms)
    name = test_path.stem
    steps = []

    try:
        lines = test_path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ValueError(f"cannot read test file {test_path}: {exc}") from exc

    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        command, _, remainder = line.partition(" ")
        command = command.lower()
        remainder = remainder.strip()
        location = f"{test_path}:{line_number}"

        try:
            if command == "name":
                if not remainder:
                    raise ValueError("name requires text")
                name = remainder
            elif command == "allow":
                parts = remainder.split()
                if len(parts) != 2:
                    raise ValueError("allow expects: allow <field> <duration>")
                field_name = _parse_field(parts[0])
                milliseconds_value = parse_duration(parts[1], milliseconds=True) * 1000
                limits[field_name] = int(milliseconds_value)
            elif command == "hold":
                parts = remainder.split()
                if len(parts) != 2:
                    raise ValueError("hold expects: hold <buttons> <duration>")
                button_names = tuple(
                    name.strip().lower()
                    for name in parts[0].replace(",", "+").split("+")
                    if name.strip()
                )
                if not button_names:
                    raise ValueError("hold requires at least one button")
                duration = parse_duration(parts[1])
                if duration <= 0:
                    raise ValueError("hold duration must be greater than zero")
                steps.append(HoldStep(ButtonInputs.from_names(button_names), duration))
            elif command == "wait":
                parts = remainder.split()
                if len(parts) != 1:
                    raise ValueError("wait expects: wait <duration>")
                duration = parse_duration(parts[0])
                if duration <= 0:
                    raise ValueError("wait duration must be greater than zero")
                steps.append(WaitStep(duration))
            else:
                raise ValueError(f"unknown command {command!r}")
        except ValueError as exc:
            raise ValueError(f"{location}: {exc}") from exc

    if not steps:
        raise ValueError(f"{test_path}: test contains no hold or wait steps")
    duration = sum(step.duration_seconds for step in steps)
    return TestCase(
        name=name,
        path=test_path,
        duration_seconds=duration,
        mismatch_limits_ms=limits,
        steps=tuple(steps),
    )


def _parse_field(value: str) -> TwinField:
    normalized = value.lower()
    if normalized in _FIELD_ALIASES:
        return _FIELD_ALIASES[normalized]
    try:
        return TwinField(normalized)
    except ValueError as exc:
        raise ValueError(f"unknown twin field {value!r}") from exc


def _validated_limits(limits: Mapping[TwinField, int]) -> dict[TwinField, int]:
    validated = {}
    for field_name, duration_ms in limits.items():
        if not isinstance(field_name, TwinField):
            raise TypeError("mismatch-limit keys must be TwinField values")
        if not isinstance(duration_ms, int) or duration_ms < 0:
            raise ValueError("mismatch limits must be non-negative integer milliseconds")
        validated[field_name] = duration_ms
    return validated
