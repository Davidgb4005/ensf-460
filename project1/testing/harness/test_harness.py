from pathlib import Path
from queue import Empty
from types import SimpleNamespace
import unittest

from testing.TUI.terminal import TuiCommand, TuiSharedState
from testing.harness.harness import TestHarness
from testing.harness.models import (
    ButtonInputs,
    FieldChange,
    HarnessConfig,
    HoldStep,
    TestCase,
    TestStatus,
    TestSuite,
    TwinEvent,
    TwinEventKind,
    TwinField,
    TwinSnapshot,
    TwinSource,
    WaitStep,
)


class FakeClock:
    def __init__(self):
        self.now_ns = 0

    def __call__(self):
        return self.now_ns

    def sleep(self, seconds):
        self.now_ns += int(seconds * 1_000_000_000)


class FakeTwin:
    def __init__(
        self,
        source,
        clock,
        *,
        follow_inputs=True,
        fixed_state="IDLE",
        emit_uart=False,
    ):
        self.source = source
        self.clock = clock
        self.follow_inputs = follow_inputs
        self.fixed_state = fixed_state
        self.emit_uart = emit_uart
        self.inputs = ButtonInputs()
        self.input_history = []
        self.sequence = 0
        self.sink = None
        self.started = False
        self.stopped = False
        self.uart_sent = False

    def snapshot(self):
        return TwinSnapshot(
            captured_at_ns=self.clock(),
            state=self.fixed_state,
            previous_state="IDLE",
            elapsed_seconds=0,
            preset_seconds=0,
            pb1=self.inputs.pb1,
            pb2=self.inputs.pb2,
            pb3=self.inputs.pb3,
        )

    def start(self, event_sink):
        self.sink = event_sink
        self.started = True
        self._emit(TwinEventKind.STARTED)

    def set_inputs(self, inputs):
        self.input_history.append(inputs)
        previous = self.inputs
        if self.follow_inputs:
            self.inputs = inputs
        changes = []
        for field_name in (TwinField.PB1, TwinField.PB2, TwinField.PB3):
            old = getattr(previous, field_name.value)
            new = getattr(self.inputs, field_name.value)
            if old != new:
                changes.append(FieldChange(field_name, old, new))
        if changes and self.sink is not None:
            self._emit(TwinEventKind.STATE_CHANGED, tuple(changes))
        if self.emit_uart and inputs.pb1 and not self.uart_sent and self.sink is not None:
            self.uart_sent = True
            self._emit(
                TwinEventKind.UART_MESSAGE,
                uart_message=f"[00:00:00.000] {self.source.value} PB1 ON",
            )

    def stop(self):
        self.stopped = True

    def _emit(self, kind, changes=(), uart_message=None):
        event = TwinEvent(
            source=self.source,
            kind=kind,
            timestamp_ns=self.clock(),
            sequence=self.sequence,
            snapshot=self.snapshot(),
            changes=changes,
            uart_message=uart_message,
        )
        self.sequence += 1
        self.sink(event)


class FakeLogger:
    def __init__(self):
        self.paths = SimpleNamespace(
            digital_uart=Path("digital-uart.log"),
            hardware_uart=Path("hardware-uart.log"),
        )
        self.states = []
        self.uart = []

    def log_state_change(self, action, digital, hardware):
        self.states.append((action, digital, hardware))

    def log_uart(self, source, message, digital, hardware):
        self.uart.append((source, message, digital, hardware))


class FakePowerProfiler:
    def __init__(self, *, fail_stop=False):
        self.started = []
        self.stop_count = 0
        self.closed = False
        self.fail_stop = fail_stop

    def start(self, label):
        self.started.append(label)

    def stop(self):
        self.stop_count += 1
        if self.fail_stop:
            raise RuntimeError("power stop failed")

    def close(self):
        self.closed = True


class ReactiveTuiState(TuiSharedState):
    def __init__(self, first_command=None, quit_on_reset_failure=False):
        super().__init__()
        self.last_dashboard = None
        self.quit_on_reset_failure = quit_on_reset_failure
        self.quit_sent = False
        if first_command is not None:
            self.command_queue.put(first_command)

    def publish(self, dashboard):
        self.last_dashboard = dashboard
        super().publish(dashboard)

    def get_command(self, *, block=False, timeout=None):
        try:
            return super().get_command(block=block, timeout=timeout)
        except Empty:
            if (
                self.quit_on_reset_failure
                and not self.quit_sent
                and self.last_dashboard is not None
                and self.last_dashboard.test.status == TestStatus.RESET_FAILED.value
            ):
                self.quit_sent = True
                return TuiCommand.QUIT
            raise


def limits(milliseconds=100):
    return {field_name: milliseconds for field_name in TwinField}


def test_case(name="test", *, duration=0.02, mismatch_limits=None, hold=False):
    steps = (
        (HoldStep(ButtonInputs(pb1=True), duration),)
        if hold
        else (WaitStep(duration),)
    )
    return TestCase(
        name=name,
        path=Path(f"{name}.txt"),
        duration_seconds=duration,
        mismatch_limits_ms=mismatch_limits or limits(),
        steps=steps,
    )


class TestHarnessTests(unittest.TestCase):
    def make_harness(
        self,
        tests,
        *,
        digital_options=None,
        hardware_options=None,
        tui_state=None,
        logger=None,
        power=None,
        config=None,
    ):
        clock = FakeClock()
        digital = FakeTwin(
            TwinSource.DIGITAL, clock, **(digital_options or {})
        )
        hardware = FakeTwin(
            TwinSource.HARDWARE, clock, **(hardware_options or {})
        )
        harness = TestHarness(
            config or HarnessConfig(tick_interval_seconds=0.005),
            TestSuite("suite", tuple(tests)),
            digital,
            hardware,
            tui_state=tui_state,
            logger=logger,
            power_profiler=power,
            clock_ns=clock,
            sleep=clock.sleep,
        )
        return harness, digital, hardware

    def test_runs_queued_tests_and_optional_components(self):
        tui_state = TuiSharedState()
        logger = FakeLogger()
        power = FakePowerProfiler()
        tests = (test_case("one"), test_case("two"))
        harness, digital, hardware = self.make_harness(
            tests,
            tui_state=tui_state,
            logger=logger,
            power=power,
            digital_options={"emit_uart": True},
        )

        result = harness.run()

        self.assertEqual(result.status, TestStatus.PASSED)
        self.assertEqual([item.status for item in result.results], [TestStatus.PASSED] * 2)
        self.assertEqual(power.started, ["one", "two"])
        self.assertEqual(power.stop_count, 2)
        self.assertTrue(power.closed)
        self.assertTrue(digital.stopped)
        self.assertTrue(hardware.stopped)
        self.assertTrue(logger.states)

    def test_routes_uart_independently_to_tui_and_logger(self):
        tui_state = TuiSharedState()
        logger = FakeLogger()
        harness, _, _ = self.make_harness(
            (test_case(hold=True),),
            tui_state=tui_state,
            logger=logger,
            digital_options={"emit_uart": True},
        )

        harness.run()
        frame = tui_state.read_frame()

        self.assertEqual(len(frame.digital_uart), 1)
        self.assertIn("digital PB1 ON", frame.digital_uart[0])
        self.assertEqual(len(logger.uart), 1)
        self.assertEqual(logger.uart[0][0], "digital")

    def test_power_profiler_failure_stops_suite_as_infrastructure_error(self):
        power = FakePowerProfiler(fail_stop=True)
        harness, _, _ = self.make_harness(
            (test_case("one"), test_case("two")), power=power
        )

        result = harness.run()

        self.assertEqual(result.status, TestStatus.ERROR)
        self.assertEqual(len(result.results), 1)
        self.assertEqual(result.results[0].status, TestStatus.ERROR)
        self.assertEqual(power.started, ["one"])

    def test_over_limit_mismatch_fails_but_test_finishes(self):
        mismatch_limits = {TwinField.PB1: 10}
        test = test_case(
            duration=0.03, mismatch_limits=mismatch_limits, hold=True
        )
        harness, digital, _ = self.make_harness(
            (test,), hardware_options={"follow_inputs": False}
        )

        result = harness.run()

        self.assertEqual(result.status, TestStatus.FAILED)
        self.assertEqual(result.results[0].status, TestStatus.FAILED)
        self.assertEqual(result.results[0].failures[0].field, TwinField.PB1)
        self.assertIn(ButtonInputs(), digital.input_history)

    def test_next_skips_current_test_and_advances(self):
        tui_state = TuiSharedState()
        tui_state.command_queue.put(TuiCommand.NEXT_TEST)
        tests = (test_case("first"), test_case("second"))
        harness, _, _ = self.make_harness(tests, tui_state=tui_state)

        result = harness.run()

        self.assertEqual(result.results[0].status, TestStatus.SKIPPED)
        self.assertEqual(result.results[1].status, TestStatus.PASSED)

    def test_restart_runs_reset_sequence_then_reruns_interrupted_test(self):
        tui_state = ReactiveTuiState(TuiCommand.RESTART_TEST)
        config = HarnessConfig(
            default_mismatch_limits_ms=limits(0),
            restart_mismatch_limits_ms=limits(0),
            tick_interval_seconds=0.01,
        )
        harness, digital, hardware = self.make_harness(
            (test_case("interrupted", duration=0.01, mismatch_limits=limits(0)),),
            tui_state=tui_state,
            config=config,
        )

        result = harness.run()

        self.assertEqual(result.status, TestStatus.PASSED)
        self.assertEqual(len(result.results), 1)
        self.assertIn(ButtonInputs(pb3=True), digital.input_history)
        self.assertIn(ButtonInputs(pb1=True, pb2=True), digital.input_history)
        self.assertIn(ButtonInputs(pb3=True), hardware.input_history)
        self.assertEqual(tui_state.last_dashboard.test.run_id, 3)

    def test_failed_reset_blocks_test_until_quit(self):
        tui_state = ReactiveTuiState(
            TuiCommand.RESTART_TEST, quit_on_reset_failure=True
        )
        logger = FakeLogger()
        config = HarnessConfig(
            default_mismatch_limits_ms=limits(10),
            restart_mismatch_limits_ms=limits(10),
            tick_interval_seconds=0.005,
        )
        harness, _, _ = self.make_harness(
            (test_case("unsafe"),),
            hardware_options={"fixed_state": "RUN"},
            tui_state=tui_state,
            logger=logger,
            config=config,
        )

        result = harness.run()

        self.assertEqual(result.status, TestStatus.ABORTED)
        self.assertEqual(result.results[0].status, TestStatus.ABORTED)
        self.assertTrue(
            any(action == "RESET_VALIDATION_FAILED" for action, _, _ in logger.states)
        )

    def test_failed_reset_without_tui_returns_error_instead_of_waiting(self):
        config = HarnessConfig(
            default_mismatch_limits_ms=limits(0),
            restart_mismatch_limits_ms=limits(0),
            tick_interval_seconds=0.005,
        )
        # Invoke the reset gate through its public trigger using a state object,
        # then remove the command channel before failed-reset waiting begins.
        tui_state = ReactiveTuiState(TuiCommand.RESTART_TEST)
        harness, _, _ = self.make_harness(
            (test_case("headless"),),
            hardware_options={"fixed_state": "RUN"},
            tui_state=tui_state,
            config=config,
        )
        original_execute_restart = harness._execute_restart_once

        def execute_then_detach(test):
            outcome = original_execute_restart(test)
            if outcome is not None and harness._status is TestStatus.RESET_FAILED:
                harness.tui_state = None
            return outcome

        harness._execute_restart_once = execute_then_detach
        result = harness.run()

        self.assertEqual(result.status, TestStatus.ERROR)
        self.assertEqual(result.results[0].status, TestStatus.ERROR)


if __name__ == "__main__":
    unittest.main()
