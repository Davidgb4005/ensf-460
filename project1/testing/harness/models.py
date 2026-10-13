"""Shared, hardware-independent models for the test harness."""

from __future__ import annotations

from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Protocol, TypeAlias


Scalar: TypeAlias = str | int | bool


class TwinSource(str, Enum):
    DIGITAL = "digital"
    HARDWARE = "hardware"


class TwinField(str, Enum):
    STATE = "state"
    PREVIOUS_STATE = "previous_state"
    ELAPSED_SECONDS = "elapsed_seconds"
    PRESET_SECONDS = "preset_seconds"
    PB1 = "pb1"
    PB2 = "pb2"
    PB3 = "pb3"
    LED1_STATE = "led1_state"
    LED1_OUTPUT = "led1_output"
    LED2_STATE = "led2_state"
    LED2_OUTPUT = "led2_output"


class TwinEventKind(str, Enum):
    STARTED = "started"
    STATE_CHANGED = "state_changed"
    UART_MESSAGE = "uart_message"
    ERROR = "error"
    STOPPED = "stopped"


class TestStatus(str, Enum):
    PENDING = "PENDING"
    RUNNING = "RUNNING"
    PASSED = "PASSED"
    FAILED = "FAILED"
    SKIPPED = "SKIPPED"
    ERROR = "ERROR"
    RESETTING = "RESETTING"
    RESET_FAILED = "RESET FAILED"
    ABORTED = "ABORTED"


@dataclass(frozen=True)
class ButtonInputs:
    pb1: bool = False
    pb2: bool = False
    pb3: bool = False

    @classmethod
    def from_names(cls, names: tuple[str, ...]) -> "ButtonInputs":
        selected = set(names)
        unknown = selected.difference({"pb1", "pb2", "pb3"})
        if unknown:
            raise ValueError(f"unknown buttons: {', '.join(sorted(unknown))}")
        return cls(pb1="pb1" in selected, pb2="pb2" in selected, pb3="pb3" in selected)


@dataclass(frozen=True)
class TwinSnapshot:
    captured_at_ns: int
    state: str
    previous_state: str
    elapsed_seconds: int
    preset_seconds: int
    pb1: bool
    pb2: bool
    pb3: bool
    led1_state: str = "OFF"
    led1_output: bool = False
    led2_state: str = "OFF"
    led2_output: bool = False

    def field_value(self, field_name: TwinField) -> Scalar:
        return getattr(self, field_name.value)

    def as_state_mapping(self) -> dict[str, Scalar]:
        return {field_name.value: self.field_value(field_name) for field_name in TwinField}


@dataclass(frozen=True)
class FieldChange:
    field: TwinField
    previous: Scalar
    current: Scalar


@dataclass(frozen=True)
class TwinEvent:
    source: TwinSource
    kind: TwinEventKind
    timestamp_ns: int
    sequence: int
    snapshot: TwinSnapshot
    changes: tuple[FieldChange, ...] = ()
    uart_message: str | None = None
    error: str | None = None


class TwinInterface(Protocol):
    @property
    def source(self) -> TwinSource:
        ...

    def start(self, event_sink: Callable[[TwinEvent], None]) -> None:
        ...

    def set_inputs(self, inputs: ButtonInputs) -> None:
        ...

    def snapshot(self) -> TwinSnapshot:
        ...

    def stop(self) -> None:
        ...


@dataclass(frozen=True)
class HoldStep:
    inputs: ButtonInputs
    duration_seconds: float


@dataclass(frozen=True)
class WaitStep:
    duration_seconds: float


TestStep: TypeAlias = HoldStep | WaitStep


@dataclass(frozen=True)
class TestCase:
    name: str
    path: Path | None
    duration_seconds: float
    mismatch_limits_ms: Mapping[TwinField, int]
    steps: tuple[TestStep, ...]


@dataclass(frozen=True)
class TestSuite:
    name: str
    tests: tuple[TestCase, ...]


@dataclass(frozen=True)
class MismatchFailure:
    field: TwinField
    allowed_ms: int
    duration_ms: int
    digital_value: Scalar
    hardware_value: Scalar


@dataclass(frozen=True)
class TestResult:
    test: TestCase
    status: TestStatus
    failures: tuple[MismatchFailure, ...] = ()
    error: str | None = None


@dataclass(frozen=True)
class SuiteResult:
    suite: TestSuite
    status: TestStatus
    results: tuple[TestResult, ...]


def default_mismatch_limits() -> dict[TwinField, int]:
    return {field_name: 100 for field_name in TwinField}


@dataclass(frozen=True)
class HarnessConfig:
    default_mismatch_limits_ms: Mapping[TwinField, int] = field(
        default_factory=default_mismatch_limits
    )
    restart_mismatch_limits_ms: Mapping[TwinField, int] | None = None
    tick_interval_seconds: float = 0.01
    hardware_uart_port: str = ""
    pb1_drive_pin: int | None = None
    pb2_drive_pin: int | None = None
    pb3_drive_pin: int | None = None
    gpio_active_high: bool = True

    def restart_limits(self) -> dict[TwinField, int]:
        selected = self.restart_mismatch_limits_ms or self.default_mismatch_limits_ms
        return dict(selected)
