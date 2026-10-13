"""Per-field mismatch timing and failure collection."""

from __future__ import annotations

from collections.abc import Mapping

from .models import MismatchFailure, TwinField, TwinSnapshot


class TwinComparator:
    def __init__(self, mismatch_limits_ms: Mapping[TwinField, int]) -> None:
        self._limits = dict(mismatch_limits_ms)
        for field_name, duration_ms in self._limits.items():
            if not isinstance(field_name, TwinField):
                raise TypeError("mismatch-limit keys must be TwinField values")
            if not isinstance(duration_ms, int) or duration_ms < 0:
                raise ValueError(
                    "mismatch limits must be non-negative integer milliseconds"
                )
        self.reset()

    @property
    def failures(self) -> tuple[MismatchFailure, ...]:
        return tuple(self._failures)

    def reset(self) -> None:
        self._mismatch_started_ns: dict[TwinField, int | None] = {
            field_name: None for field_name in self._limits
        }
        self._failed_episode: set[TwinField] = set()
        self._failures: list[MismatchFailure] = []

    def update(
        self,
        digital: TwinSnapshot,
        hardware: TwinSnapshot,
        now_ns: int,
    ) -> tuple[MismatchFailure, ...]:
        new_failures = []
        for field_name, allowed_ms in self._limits.items():
            digital_value = digital.field_value(field_name)
            hardware_value = hardware.field_value(field_name)
            if digital_value == hardware_value:
                self._mismatch_started_ns[field_name] = None
                self._failed_episode.discard(field_name)
                continue

            started_ns = self._mismatch_started_ns[field_name]
            if started_ns is None:
                started_ns = now_ns
                self._mismatch_started_ns[field_name] = started_ns
            duration_ms = max(0, (now_ns - started_ns) // 1_000_000)
            if duration_ms >= allowed_ms and field_name not in self._failed_episode:
                failure = MismatchFailure(
                    field=field_name,
                    allowed_ms=allowed_ms,
                    duration_ms=duration_ms,
                    digital_value=digital_value,
                    hardware_value=hardware_value,
                )
                self._failures.append(failure)
                self._failed_episode.add(field_name)
                new_failures.append(failure)
        return tuple(new_failures)

    def exact_matches(
        self, digital: TwinSnapshot, hardware: TwinSnapshot
    ) -> bool:
        return all(
            digital.field_value(field_name) == hardware.field_value(field_name)
            for field_name in self._limits
        )
