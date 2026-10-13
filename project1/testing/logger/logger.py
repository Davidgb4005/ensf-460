"""Independent event logger for twin state changes and UART messages."""

from __future__ import annotations

import csv
from dataclasses import asdict, dataclass, is_dataclass
from datetime import datetime, timezone
from enum import Enum
import json
from pathlib import Path
import threading
from typing import Callable, Mapping


StateSnapshot = Mapping[str, object]


@dataclass(frozen=True)
class LoggerPaths:
    """The three paths supplied by the test rig's top-level configuration."""

    digital_uart: str | Path
    hardware_uart: str | Path
    state_events: str | Path


class TestRigLogger:
    """Write UART streams and complete event snapshots to three files."""

    STATE_COLUMNS = (
        "timestamp_utc",
        "action",
        "digital_state",
        "hardware_state",
        "uart_source",
        "uart_message",
    )
    UART_SOURCES = {"digital", "hardware"}

    def __init__(
        self,
        paths: LoggerPaths,
        *,
        clock: Callable[[], datetime] | None = None,
    ) -> None:
        if not isinstance(paths, LoggerPaths):
            raise TypeError("paths must be a LoggerPaths instance")

        self.paths = LoggerPaths(
            digital_uart=Path(paths.digital_uart),
            hardware_uart=Path(paths.hardware_uart),
            state_events=Path(paths.state_events),
        )
        self._clock = clock or (lambda: datetime.now(timezone.utc))
        self._lock = threading.Lock()
        self._prepare_files()

    def log_state_change(
        self,
        action: str,
        digital_state: StateSnapshot,
        hardware_state: StateSnapshot,
    ) -> None:
        """Write one state-change record to the full-state event log."""

        self._validate_action(action)
        row = self._event_row(action, digital_state, hardware_state)
        with self._lock:
            self._append_state_row(row)

    def log_uart(
        self,
        source: str,
        message: str,
        digital_state: StateSnapshot,
        hardware_state: StateSnapshot,
    ) -> None:
        """Write a UART line and its complete state to the appropriate logs."""

        source = source.lower()
        if source not in self.UART_SOURCES:
            raise ValueError("source must be 'digital' or 'hardware'")
        if not isinstance(message, str):
            raise TypeError("message must be a string")

        row = self._event_row(
            f"{source.upper()}_UART_MESSAGE",
            digital_state,
            hardware_state,
            uart_source=source,
            uart_message=message,
        )
        uart_path = (
            self.paths.digital_uart
            if source == "digital"
            else self.paths.hardware_uart
        )
        with self._lock:
            self._append_uart(uart_path, message)
            self._append_state_row(row)

    def _event_row(
        self,
        action: str,
        digital_state: StateSnapshot,
        hardware_state: StateSnapshot,
        *,
        uart_source: str = "",
        uart_message: str = "",
    ) -> dict[str, str]:
        return {
            "timestamp_utc": self._timestamp(),
            "action": action,
            "digital_state": self._serialize_state(digital_state),
            "hardware_state": self._serialize_state(hardware_state),
            "uart_source": uart_source,
            "uart_message": uart_message,
        }

    def _prepare_files(self) -> None:
        for path in (
            self.paths.digital_uart,
            self.paths.hardware_uart,
            self.paths.state_events,
        ):
            path.parent.mkdir(parents=True, exist_ok=True)

        self.paths.digital_uart.touch(exist_ok=True)
        self.paths.hardware_uart.touch(exist_ok=True)
        if not self.paths.state_events.exists() or self.paths.state_events.stat().st_size == 0:
            with self.paths.state_events.open("w", newline="", encoding="utf-8") as log:
                csv.DictWriter(log, fieldnames=self.STATE_COLUMNS).writeheader()

    def _append_state_row(self, row: dict[str, str]) -> None:
        with self.paths.state_events.open("a", newline="", encoding="utf-8") as log:
            csv.DictWriter(log, fieldnames=self.STATE_COLUMNS).writerow(row)

    @staticmethod
    def _append_uart(path: Path, message: str) -> None:
        with path.open("a", encoding="utf-8") as log:
            log.write(message)
            if not message.endswith(("\n", "\r")):
                log.write("\n")

    def _timestamp(self) -> str:
        value = self._clock()
        if not isinstance(value, datetime):
            raise TypeError("clock must return a datetime")
        if value.tzinfo is None:
            value = value.replace(tzinfo=timezone.utc)
        value = value.astimezone(timezone.utc)
        return value.isoformat(timespec="milliseconds").replace("+00:00", "Z")

    @classmethod
    def _serialize_state(cls, state: StateSnapshot) -> str:
        if not isinstance(state, Mapping):
            raise TypeError("twin state must be a mapping")
        return json.dumps(
            state,
            default=cls._json_default,
            sort_keys=True,
            separators=(",", ":"),
        )

    @staticmethod
    def _json_default(value: object) -> object:
        if isinstance(value, Enum):
            return value.name
        if is_dataclass(value) and not isinstance(value, type):
            return asdict(value)
        raise TypeError(f"state value is not JSON serializable: {type(value).__name__}")

    @staticmethod
    def _validate_action(action: str) -> None:
        if not isinstance(action, str) or not action.strip():
            raise ValueError("action must be a non-empty string")
