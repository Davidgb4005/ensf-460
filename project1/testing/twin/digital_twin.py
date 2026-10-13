"""Harness adapter for the existing digital stopwatch model."""

from __future__ import annotations

import threading
import time
from typing import Callable

from testing.harness.models import (
    ButtonInputs,
    FieldChange,
    TwinEvent,
    TwinEventKind,
    TwinField,
    TwinSnapshot,
    TwinSource,
)

from .protocol import LED_STATES, TIMER_STATES, StatusFrame, format_frame


_TIMER_CODES = {name: code for code, name in TIMER_STATES.items()}
_LED_CODES = {name: code for code, name in LED_STATES.items()}


class DigitalTwin:
    """Expose an existing ``StopWatch`` through ``TwinInterface``.

    The wrapped model's ``main`` method remains the owner of all timer logic.
    This adapter only applies button levels, observes public model attributes,
    and turns changes into harness events.
    """

    source = TwinSource.DIGITAL

    def __init__(
        self,
        stopwatch,
        *,
        poll_interval_seconds: float = 0.005,
        clock_ns: Callable[[], int] = time.monotonic_ns,
    ) -> None:
        if poll_interval_seconds <= 0:
            raise ValueError("poll_interval_seconds must be greater than zero")
        self.stopwatch = stopwatch
        self.poll_interval_seconds = poll_interval_seconds
        self._clock_ns = clock_ns
        self._event_sink = None
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._lock = threading.RLock()
        self._sequence = 0
        initial_state = self._enum_name(stopwatch.state, "TIMER_")
        self._last_model_state = initial_state
        self._previous_state = initial_state
        self._last_snapshot: TwinSnapshot | None = None
        self._model_started = False

    def start(self, event_sink: Callable[[TwinEvent], None]) -> None:
        with self._lock:
            if self._thread is not None:
                raise RuntimeError("digital twin is already running")
            self._event_sink = event_sink
            self._stop_event.clear()
            if not self._model_started:
                self.stopwatch.main()
                self._model_started = True
            snapshot = self._capture_snapshot()
            self._last_snapshot = snapshot
            self._emit(TwinEventKind.STARTED, snapshot)
            self._emit_uart(snapshot)
            self._thread = threading.Thread(
                target=self._poll_loop,
                name="digital-twin",
                daemon=True,
            )
            self._thread.start()

    def set_inputs(self, inputs: ButtonInputs) -> None:
        with self._lock:
            self._set_button(self.stopwatch.pb_1, inputs.pb1)
            self._set_button(self.stopwatch.pb_2, inputs.pb2)
            self._set_button(self.stopwatch.pb_3, inputs.pb3)
        self._publish_changes()

    def snapshot(self) -> TwinSnapshot:
        with self._lock:
            return self._capture_snapshot()

    def stop(self) -> None:
        with self._lock:
            thread = self._thread
            if thread is None:
                return
            self._stop_event.set()
        thread.join(timeout=2.0)
        with self._lock:
            self._thread = None
            self._set_button(self.stopwatch.pb_1, False)
            self._set_button(self.stopwatch.pb_2, False)
            self._set_button(self.stopwatch.pb_3, False)
            snapshot = self._capture_snapshot()
            self._last_snapshot = snapshot
            self._emit(TwinEventKind.STOPPED, snapshot)

    def _poll_loop(self) -> None:
        try:
            while not self._stop_event.wait(self.poll_interval_seconds):
                self._publish_changes()
        except BaseException as exc:
            with self._lock:
                snapshot = self._last_snapshot or self._capture_snapshot()
                self._emit(TwinEventKind.ERROR, snapshot, error=str(exc))
            self._stop_event.set()

    def _publish_changes(self) -> None:
        with self._lock:
            snapshot = self._capture_snapshot()
            previous = self._last_snapshot
            if previous is None:
                self._last_snapshot = snapshot
                return
            changes = _snapshot_changes(previous, snapshot)
            if not changes:
                return
            self._last_snapshot = snapshot
            self._emit(TwinEventKind.STATE_CHANGED, snapshot, changes=changes)
            self._emit_uart(snapshot)

    def _capture_snapshot(self) -> TwinSnapshot:
        state = self._enum_name(self.stopwatch.state, "TIMER_")
        if state != self._last_model_state:
            self._previous_state = self._last_model_state
            self._last_model_state = state
        return TwinSnapshot(
            captured_at_ns=self._clock_ns(),
            state=state,
            previous_state=self._previous_state,
            elapsed_seconds=int(self.stopwatch.elapsed_timer),
            preset_seconds=int(self.stopwatch.preset_timer),
            pb1=self._is_on(self.stopwatch.pb_1.state),
            pb2=self._is_on(self.stopwatch.pb_2.state),
            pb3=self._is_on(self.stopwatch.pb_3.state),
            led1_state=self._enum_name(self.stopwatch.led_1.state, "GPIO_"),
            led1_output=self._is_on(self.stopwatch.led_1.output),
            led2_state=self._enum_name(self.stopwatch.led_2.state, "GPIO_"),
            led2_output=self._is_on(self.stopwatch.led_2.output),
        )

    @staticmethod
    def _set_button(button, pressed: bool) -> None:
        state_type = type(button.state)
        target = state_type.ON if pressed else state_type.OFF
        if button.state == target:
            return
        now = time.perf_counter()
        button.state_duration = now - button.timer_start
        button.timer_start = now
        button.state = target
        edge_type = type(button.last_edge)
        button.last_edge = (
            edge_type.RISING_EDGE if pressed else edge_type.FALLING_EDGE
        )

    @staticmethod
    def _is_on(value) -> bool:
        return getattr(value, "name", str(value)).removeprefix("GPIO_") == "ON"

    @staticmethod
    def _enum_name(value, prefix: str) -> str:
        return getattr(value, "name", str(value)).removeprefix(prefix)

    def _emit_uart(self, snapshot: TwinSnapshot) -> None:
        frame = StatusFrame(
            elapsed_seconds=snapshot.elapsed_seconds,
            preset_seconds=snapshot.preset_seconds,
            state_code=_TIMER_CODES.get(snapshot.state, 0),
            previous_state_code=_TIMER_CODES.get(snapshot.previous_state, 0),
            pb1=snapshot.pb1,
            pb2=snapshot.pb2,
            pb3=snapshot.pb3,
            led1_state_code=_LED_CODES.get(snapshot.led1_state, 0),
            led2_state_code=_LED_CODES.get(snapshot.led2_state, 0),
        )
        self._emit(
            TwinEventKind.UART_MESSAGE,
            snapshot,
            uart_message=format_frame(frame),
        )

    def _emit(
        self,
        kind: TwinEventKind,
        snapshot: TwinSnapshot,
        *,
        changes: tuple[FieldChange, ...] = (),
        uart_message: str | None = None,
        error: str | None = None,
    ) -> None:
        sink = self._event_sink
        if sink is None:
            return
        event = TwinEvent(
            source=self.source,
            kind=kind,
            timestamp_ns=self._clock_ns(),
            sequence=self._sequence,
            snapshot=snapshot,
            changes=changes,
            uart_message=uart_message,
            error=error,
        )
        self._sequence += 1
        sink(event)


def _snapshot_changes(
    previous: TwinSnapshot, current: TwinSnapshot
) -> tuple[FieldChange, ...]:
    changes = []
    for field_name in TwinField:
        before = previous.field_value(field_name)
        after = current.field_value(field_name)
        if before != after:
            changes.append(FieldChange(field_name, before, after))
    return tuple(changes)
