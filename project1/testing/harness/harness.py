"""Single-loop coordinator for test scheduling, comparison, and event routing."""

from __future__ import annotations

from enum import Enum, auto
from queue import Empty, SimpleQueue
import time
from typing import Callable

from testing.TUI.terminal import (
    DashboardData,
    GpioDriveData,
    OutputData,
    TestData,
    TuiCommand,
    TwinData,
)

from .comparator import TwinComparator
from .models import (
    ButtonInputs,
    HarnessConfig,
    HoldStep,
    MismatchFailure,
    SuiteResult,
    TestCase,
    TestResult,
    TestStatus,
    TestSuite,
    TwinEvent,
    TwinEventKind,
    TwinField,
    TwinInterface,
    TwinSnapshot,
    TwinSource,
)


MAX_EVENTS_PER_SERVICE = 64


class _Control(Enum):
    COMPLETE = auto()
    RESTART = auto()
    NEXT = auto()
    QUIT = auto()
    ERROR = auto()


class TestHarness:
    """Run a test suite and fan twin events out to optional consumers."""

    def __init__(
        self,
        config: HarnessConfig,
        suite: TestSuite,
        digital_twin: TwinInterface,
        hardware_twin: TwinInterface,
        *,
        tui_state=None,
        tui=None,
        logger=None,
        power_profiler=None,
        clock_ns: Callable[[], int] = time.monotonic_ns,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if digital_twin.source is not TwinSource.DIGITAL:
            raise ValueError("digital_twin must have source DIGITAL")
        if hardware_twin.source is not TwinSource.HARDWARE:
            raise ValueError("hardware_twin must have source HARDWARE")
        if config.tick_interval_seconds <= 0:
            raise ValueError("tick_interval_seconds must be greater than zero")

        self.config = config
        self.suite = suite
        self.digital_twin = digital_twin
        self.hardware_twin = hardware_twin
        self.tui_state = tui_state
        self.tui = tui
        self.logger = logger
        self.power_profiler = power_profiler
        self._clock_ns = clock_ns
        self._sleep = sleep

        self._events: SimpleQueue[TwinEvent] = SimpleQueue()
        self._latest: dict[TwinSource, TwinSnapshot] = {}
        self._last_sequence = {TwinSource.DIGITAL: -1, TwinSource.HARDWARE: -1}
        self._comparator: TwinComparator | None = None
        self._run_id = 0
        self._background_error: str | None = None
        self._current_test: TestCase | None = None
        self._next_test: TestCase | None = None
        self._status = TestStatus.PENDING
        self._power_status = "DISABLED" if power_profiler is None else "IDLE"
        self._driven_inputs = ButtonInputs()

    def run(self) -> SuiteResult:
        results: list[TestResult] = []
        aborted = False
        infrastructure_error = False

        try:
            if self.tui is not None:
                self.tui.start()
            self.digital_twin.start(self._events.put)
            self.hardware_twin.start(self._events.put)
            self._latest[TwinSource.DIGITAL] = self.digital_twin.snapshot()
            self._latest[TwinSource.HARDWARE] = self.hardware_twin.snapshot()
            self._service()

            index = 0
            while index < len(self.suite.tests):
                test = self.suite.tests[index]
                next_test = (
                    self.suite.tests[index + 1]
                    if index + 1 < len(self.suite.tests)
                    else None
                )

                while True:
                    control, result = self._execute_test(test, next_test)
                    if control is _Control.RESTART:
                        restart_control = self._run_restart_gate(test)
                        if restart_control is _Control.COMPLETE:
                            continue
                        control = restart_control

                    if result is not None:
                        results.append(result)
                    elif control is _Control.NEXT:
                        results.append(TestResult(test, TestStatus.SKIPPED))
                    elif control is _Control.QUIT:
                        results.append(TestResult(test, TestStatus.ABORTED))
                    elif control is _Control.ERROR:
                        results.append(
                            TestResult(
                                test,
                                TestStatus.ERROR,
                                error=self._background_error or "harness error",
                            )
                        )

                    if control is _Control.QUIT:
                        aborted = True
                    if control is _Control.ERROR:
                        infrastructure_error = True
                    break

                if aborted or infrastructure_error:
                    break
                index += 1
        except Exception as exc:
            infrastructure_error = True
            self._background_error = str(exc)
        finally:
            self._release_inputs()
            self._stop_components()

        if infrastructure_error:
            suite_status = TestStatus.ERROR
        elif aborted:
            suite_status = TestStatus.ABORTED
        elif any(result.status is TestStatus.FAILED for result in results):
            suite_status = TestStatus.FAILED
        else:
            suite_status = TestStatus.PASSED
        self._status = suite_status
        self._publish_dashboard()
        return SuiteResult(self.suite, suite_status, tuple(results))

    def _execute_test(
        self, test: TestCase, next_test: TestCase | None
    ) -> tuple[_Control, TestResult | None]:
        self._current_test = test
        self._next_test = next_test
        self._status = TestStatus.PENDING
        self._background_error = None
        self._comparator = TwinComparator(test.mismatch_limits_ms)
        self._release_inputs()
        self._publish_dashboard()

        control = _Control.COMPLETE
        result = None
        try:
            self._start_power(test.name)
            self._run_id += 1
            self._status = TestStatus.RUNNING
            self._publish_dashboard()
            for step in test.steps:
                if isinstance(step, HoldStep):
                    self._set_inputs(step.inputs)
                    step_control = self._wait_for(step.duration_seconds)
                    self._release_inputs()
                else:
                    step_control = self._wait_for(step.duration_seconds)
                if step_control is not _Control.COMPLETE:
                    control = step_control
                    break

            if control is _Control.COMPLETE:
                self._service()
                if self._background_error is not None:
                    control = _Control.ERROR
                else:
                    failures = self._comparator.failures
                    status = TestStatus.FAILED if failures else TestStatus.PASSED
                    self._status = status
                    self._publish_dashboard()
                    result = TestResult(test, status, failures)
        except Exception as exc:
            self._background_error = str(exc)
            self._status = TestStatus.ERROR
            self._publish_dashboard()
            control = _Control.ERROR
        finally:
            self._release_inputs()
            self._stop_power()

        if self._background_error is not None and control is _Control.COMPLETE:
            self._status = TestStatus.ERROR
            self._publish_dashboard()
            return _Control.ERROR, None
        return control, result

    def _run_restart_gate(self, interrupted_test: TestCase) -> _Control:
        while True:
            outcome = self._execute_restart_once(interrupted_test)
            if outcome in (_Control.COMPLETE, _Control.QUIT, _Control.ERROR):
                return outcome

            # Reset validation failed. Only Restart and Quit are meaningful now.
            if self.tui_state is None:
                self._background_error = "reset validation failed"
                return _Control.ERROR
            while True:
                self._service()
                command = self._poll_command()
                if command is TuiCommand.RESTART_TEST:
                    break
                if command is TuiCommand.QUIT:
                    return _Control.QUIT
                self._sleep(self.config.tick_interval_seconds)

    def _execute_restart_once(self, interrupted_test: TestCase) -> _Control:
        restart_limits = self.config.restart_limits()
        settle_seconds = max(restart_limits.values(), default=0) / 1000
        restart_test = TestCase(
            name="Restart",
            path=None,
            duration_seconds=8.0 + settle_seconds,
            mismatch_limits_ms=restart_limits,
            steps=(),
        )
        self._run_id += 1
        self._current_test = restart_test
        self._next_test = interrupted_test
        self._status = TestStatus.RESETTING
        self._comparator = None
        self._release_inputs()
        self._publish_dashboard()

        self._set_inputs(ButtonInputs(pb3=True))
        control = self._wait_for(4.0, quit_only=True)
        self._release_inputs()
        if control is not _Control.COMPLETE:
            return control

        self._set_inputs(ButtonInputs(pb1=True, pb2=True))
        control = self._wait_for(4.0, quit_only=True)
        self._release_inputs()
        if control is not _Control.COMPLETE:
            return control

        self._comparator = TwinComparator(restart_limits)
        control = self._wait_for(settle_seconds, quit_only=True)
        if control is not _Control.COMPLETE:
            return control
        self._service()

        digital, hardware = self._snapshot_pair()
        valid = (
            not self._comparator.failures
            and self._comparator.exact_matches(digital, hardware)
            and self._is_default_snapshot(digital)
            and self._is_default_snapshot(hardware)
        )
        if valid:
            return _Control.COMPLETE

        self._status = TestStatus.RESET_FAILED
        self._publish_dashboard()
        if self.logger is not None:
            self.logger.log_state_change(
                "RESET_VALIDATION_FAILED",
                digital.as_state_mapping(),
                hardware.as_state_mapping(),
            )
        return _Control.RESTART

    def _wait_for(self, duration_seconds: float, *, quit_only: bool = False) -> _Control:
        deadline_ns = self._clock_ns() + int(duration_seconds * 1_000_000_000)
        while True:
            self._service()
            if self._background_error is not None:
                return _Control.ERROR

            command = self._poll_command()
            if command is TuiCommand.QUIT:
                return _Control.QUIT
            if not quit_only:
                if command is TuiCommand.RESTART_TEST:
                    return _Control.RESTART
                if command is TuiCommand.NEXT_TEST:
                    return _Control.NEXT

            now_ns = self._clock_ns()
            if now_ns >= deadline_ns:
                return _Control.COMPLETE
            remaining_seconds = (deadline_ns - now_ns) / 1_000_000_000
            self._sleep(min(self.config.tick_interval_seconds, remaining_seconds))

    def _service(self) -> None:
        tui_error = getattr(self.tui, "error", None)
        if tui_error is not None:
            self._background_error = f"TUI failed: {tui_error}"
        # UART can arrive continuously. Draining until the queue is empty can
        # starve snapshot publication, command handling, and test deadlines if
        # the producer is faster than the logger. Process a bounded batch and
        # continue with the rest of the harness cycle.
        for _ in range(MAX_EVENTS_PER_SERVICE):
            try:
                event = self._events.get_nowait()
            except Empty:
                break
            self._route_event(event)

        # Snapshots are the authoritative live state. Events remain responsible
        # for UART display and logging, but the dashboard must not freeze if a
        # twin coalesces or misses a state-change notification.
        for source, twin in (
            (TwinSource.DIGITAL, self.digital_twin),
            (TwinSource.HARDWARE, self.hardware_twin),
        ):
            try:
                self._latest[source] = twin.snapshot()
            except Exception as exc:
                self._background_error = f"{source.value} twin snapshot failed: {exc}"

        if self._comparator is not None and len(self._latest) == 2:
            digital, hardware = self._snapshot_pair()
            new_failures = self._comparator.update(
                digital, hardware, self._clock_ns()
            )
            if new_failures and self._status is TestStatus.RUNNING:
                self._status = TestStatus.FAILED
        self._publish_dashboard()

    def _route_event(self, event: TwinEvent) -> None:
        if event.sequence <= self._last_sequence[event.source]:
            return
        self._last_sequence[event.source] = event.sequence
        self._latest[event.source] = event.snapshot

        if event.kind is TwinEventKind.ERROR:
            self._background_error = event.error or f"{event.source.value} twin error"

        if self.tui_state is not None and event.uart_message is not None:
            if event.source is TwinSource.DIGITAL:
                self.tui_state.append_digital_uart(event.uart_message)
            else:
                self.tui_state.append_hardware_uart(event.uart_message)

        if self.logger is None or len(self._latest) != 2:
            return
        digital, hardware = self._snapshot_pair()
        digital_state = digital.as_state_mapping()
        hardware_state = hardware.as_state_mapping()
        if event.kind is TwinEventKind.UART_MESSAGE and event.uart_message is not None:
            self.logger.log_uart(
                event.source.value,
                event.uart_message,
                digital_state,
                hardware_state,
            )
        else:
            self.logger.log_state_change(
                self._event_action(event), digital_state, hardware_state
            )

    @staticmethod
    def _event_action(event: TwinEvent) -> str:
        if event.changes:
            changes = "; ".join(
                f"{change.field.value}:{change.previous}->{change.current}"
                for change in event.changes
            )
            return f"{event.source.value.upper()}_{event.kind.value.upper()} {changes}"
        if event.kind is TwinEventKind.ERROR and event.error:
            return f"{event.source.value.upper()}_ERROR {event.error}"
        return f"{event.source.value.upper()}_{event.kind.value.upper()}"

    def _set_inputs(self, inputs: ButtonInputs) -> None:
        try:
            self.digital_twin.set_inputs(inputs)
            self.hardware_twin.set_inputs(inputs)
            self._driven_inputs = inputs
        except Exception:
            self._release_inputs()
            raise

    def _release_inputs(self) -> None:
        released = ButtonInputs()
        for twin in (self.digital_twin, self.hardware_twin):
            try:
                twin.set_inputs(released)
            except Exception:
                pass
        self._driven_inputs = released

    def _snapshot_pair(self) -> tuple[TwinSnapshot, TwinSnapshot]:
        return self._latest[TwinSource.DIGITAL], self._latest[TwinSource.HARDWARE]

    @staticmethod
    def _is_default_snapshot(snapshot: TwinSnapshot) -> bool:
        return (
            snapshot.state == "IDLE"
            and snapshot.elapsed_seconds == 0
            and snapshot.preset_seconds == 0
            and not snapshot.pb1
            and not snapshot.pb2
            and not snapshot.pb3
            and not snapshot.led1_output
            and not snapshot.led2_output
        )

    def _poll_command(self):
        if self.tui_state is None:
            return None
        try:
            return self.tui_state.get_command()
        except Empty:
            return None

    def _publish_dashboard(self) -> None:
        if self.tui_state is None or len(self._latest) != 2:
            return
        digital, hardware = self._snapshot_pair()
        current = self._current_test
        following = self._next_test
        self.tui_state.publish(
            DashboardData(
                digital=self._tui_twin(digital),
                hardware=self._tui_twin(hardware),
                test=TestData(
                    run_id=self._run_id or None,
                    current_name=current.name if current else "",
                    current_path=str(current.path) if current and current.path else "",
                    next_name=following.name if following else "",
                    next_path=str(following.path) if following and following.path else "",
                    duration_seconds=current.duration_seconds if current else 0.0,
                    status=self._status.value,
                ),
                outputs=OutputData(
                    hardware_uart_path=self._logger_path("hardware_uart"),
                    digital_uart_path=self._logger_path("digital_uart"),
                    power_profiler_status=self._power_status,
                    hardware_uart_port=self.config.hardware_uart_port,
                ),
                gpio_drive=GpioDriveData(
                    pb1_pin=self.config.pb1_drive_pin,
                    pb2_pin=self.config.pb2_drive_pin,
                    pb3_pin=self.config.pb3_drive_pin,
                    pb1_active=self._driven_inputs.pb1,
                    pb2_active=self._driven_inputs.pb2,
                    pb3_active=self._driven_inputs.pb3,
                    active_high=self.config.gpio_active_high,
                ),
            )
        )

    @staticmethod
    def _tui_twin(snapshot: TwinSnapshot) -> TwinData:
        return TwinData(
            state=snapshot.state,
            previous_state=snapshot.previous_state,
            elapsed_seconds=snapshot.elapsed_seconds,
            preset_seconds=snapshot.preset_seconds,
            pb1="ON" if snapshot.pb1 else "OFF",
            pb2="ON" if snapshot.pb2 else "OFF",
            pb3="ON" if snapshot.pb3 else "OFF",
        )

    def _logger_path(self, name: str) -> str:
        paths = getattr(self.logger, "paths", None)
        return str(getattr(paths, name, "")) if paths is not None else ""

    def _start_power(self, label: str) -> None:
        if self.power_profiler is None:
            return
        self._power_status = "STARTING"
        self._publish_dashboard()
        self.power_profiler.start(label)
        self._power_status = "RUNNING"
        self._publish_dashboard()

    def _stop_power(self) -> None:
        if self.power_profiler is None or self._power_status == "DISABLED":
            return
        try:
            self.power_profiler.stop()
            self._power_status = "IDLE"
        except Exception as exc:
            self._power_status = f"ERROR: {exc}"
            self._background_error = str(exc)
        self._publish_dashboard()

    def _stop_components(self) -> None:
        # Ask curses to leave raw mode first so an interrupted run restores the
        # user's terminal while the hardware resources are being released.
        if self.tui is not None:
            self.tui.stop()
        for twin in (self.digital_twin, self.hardware_twin):
            try:
                twin.stop()
            except Exception:
                pass
        if self.power_profiler is not None:
            try:
                self.power_profiler.close()
            except Exception:
                pass
        if self.tui is not None:
            self.tui.join(timeout=2.0)
