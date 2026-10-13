import queue
import unittest
from unittest.mock import patch

from testing.TUI import terminal


class FakeClock:
    def __init__(self, now=0.0):
        self.now = now

    def __call__(self):
        return self.now


class FakeWindow:
    def __init__(self, height, width, keys=None, refresh_trace=None, name="root"):
        self.height = height
        self.width = width
        self.keys = list(keys or [])
        self.children = []
        self.text = []
        self.refresh_trace = refresh_trace if refresh_trace is not None else []
        self.name = name
        self.timeout_ms = None
        self.keypad_enabled = False
        self.refreshed = False

    def getmaxyx(self):
        return self.height, self.width

    def erase(self):
        pass

    def box(self):
        pass

    def addnstr(self, row, column, text, count):
        self.text.append((row, column, str(text)[:count]))

    def refresh(self):
        self.refreshed = True

    def noutrefresh(self):
        self.refresh_trace.append(self.name)

    def derwin(self, height, width, row, column):
        child = FakeWindow(
            height,
            width,
            refresh_trace=self.refresh_trace,
            name=f"child-{len(self.children)}",
        )
        child.origin = (row, column)
        self.children.append(child)
        return child

    def keypad(self, enabled):
        self.keypad_enabled = enabled

    def timeout(self, milliseconds):
        self.timeout_ms = milliseconds

    def getch(self):
        return self.keys.pop(0) if self.keys else -1


def make_frame(dashboard=None, digital_uart=(), hardware_uart=()):
    return terminal.TuiFrame(
        dashboard=dashboard or terminal.DashboardData(),
        digital_uart=tuple(digital_uart),
        hardware_uart=tuple(hardware_uart),
    )


class SharedStateTests(unittest.TestCase):
    def test_snapshot_and_uart_messages_are_consumed_atomically(self):
        state = terminal.TuiSharedState()
        dashboard = terminal.DashboardData(
            test=terminal.TestData(run_id=7, current_name="test-seven")
        )

        state.publish(dashboard)
        state.append_digital_uart("[1.000] digital")
        state.append_hardware_uart("[1.001] hardware")

        frame = state.read_frame()
        self.assertIs(frame.dashboard, dashboard)
        self.assertEqual(frame.digital_uart, ("[1.000] digital",))
        self.assertEqual(frame.hardware_uart, ("[1.001] hardware",))

        drained = state.read_frame()
        self.assertEqual(drained.digital_uart, ())
        self.assertEqual(drained.hardware_uart, ())

    def test_input_types_are_validated(self):
        state = terminal.TuiSharedState()
        with self.assertRaises(TypeError):
            state.publish(object())
        with self.assertRaises(TypeError):
            state.append_digital_uart(123)

    def test_command_queue_preserves_repeated_command_order(self):
        state = terminal.TuiSharedState()
        expected = (
            terminal.TuiCommand.RESTART_TEST,
            terminal.TuiCommand.RESTART_TEST,
            terminal.TuiCommand.NEXT_TEST,
        )
        for command in expected:
            state.command_queue.put(command)
        self.assertEqual(tuple(state.get_command() for _ in expected), expected)
        with self.assertRaises(queue.Empty):
            state.get_command()


class DashboardModelTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock(100.0)
        self.model = terminal.DashboardModel(self.clock)

    def test_test_clock_is_derived_from_run_id_and_fixed_duration(self):
        dashboard = terminal.DashboardData(
            test=terminal.TestData(run_id=1, duration_seconds=65, status="RUNNING")
        )
        first = self.model.update(make_frame(dashboard))
        self.assertEqual(first.status.test_time_elapsed, "00:00:00")
        self.assertEqual(first.status.time_remaining, "00:01:05")

        self.clock.now = 112.9
        later = self.model.update(make_frame(dashboard))
        self.assertEqual(later.status.test_time_elapsed, "00:00:12")
        self.assertEqual(later.status.time_remaining, "00:00:52")

        self.clock.now = 170.0
        completed = self.model.update(make_frame(dashboard))
        self.assertEqual(completed.status.test_time_elapsed, "00:01:10")
        self.assertEqual(completed.status.time_remaining, "00:00:00")

        restarted = terminal.DashboardData(
            test=terminal.TestData(run_id=2, duration_seconds=30, status="RUNNING")
        )
        reset = self.model.update(make_frame(restarted))
        self.assertEqual(reset.status.test_time_elapsed, "00:00:00")
        self.assertEqual(reset.status.time_remaining, "00:00:30")

    def test_each_field_has_an_exact_match_millisecond_timer(self):
        digital = terminal.TwinData(
            state="RUN",
            previous_state="IDLE",
            elapsed_seconds=3,
            preset_seconds=10,
            pb1="ON",
            pb2="ON",
            pb3="ON",
        )
        hardware = terminal.TwinData()
        dashboard = terminal.DashboardData(
            digital=digital,
            hardware=hardware,
            test=terminal.TestData(run_id=1),
        )

        first = self.model.update(make_frame(dashboard))
        for field in terminal.COMPARED_FIELDS:
            self.assertEqual(first.differences[field], "0ms")

        self.clock.now = 100.5
        later = self.model.update(make_frame(dashboard))
        for field in ("state", "elapsed_seconds", "preset_seconds", "pb1", "pb2", "pb3"):
            self.assertEqual(later.differences[field], "500ms")
        self.assertEqual(later.differences["previous_state"], "0ms")

        matched = terminal.DashboardData(
            digital=digital,
            hardware=digital,
            test=terminal.TestData(run_id=1),
        )
        reset = self.model.update(make_frame(matched))
        self.assertTrue(all(value == "0ms" for value in reset.differences.values()))

        self.clock.now = 101.0
        restarted = terminal.DashboardData(
            digital=digital,
            hardware=hardware,
            test=terminal.TestData(run_id=2),
        )
        new_run = self.model.update(make_frame(restarted))
        self.assertTrue(all(value == "0ms" for value in new_run.differences.values()))

    def test_uart_is_newest_first_and_trimmed_to_visible_capacity(self):
        first = self.model.update(
            make_frame(
                digital_uart=("[1] oldest", "[2] newer"),
                hardware_uart=("[1] hardware",),
            ),
            serial_capacity=2,
        )
        self.assertEqual(first.digital_uart, ("[2] newer", "[1] oldest"))
        self.assertEqual(first.hardware_uart, ("[1] hardware",))

        second = self.model.update(
            make_frame(digital_uart=("[3] newest",)), serial_capacity=2
        )
        self.assertEqual(second.digital_uart, ("[3] newest", "[2] newer"))

    def test_uart_backlog_is_bounded_while_terminal_is_too_small(self):
        messages = tuple(
            f"[{index}] message" for index in range(terminal.MAX_UART_BACKLOG + 1)
        )
        view = self.model.update(make_frame(digital_uart=messages))
        self.assertEqual(len(view.digital_uart), terminal.MAX_UART_BACKLOG)
        self.assertEqual(view.digital_uart[0], messages[-1])
        self.assertNotIn(messages[0], view.digital_uart)

    def test_raw_twin_times_are_formatted_by_the_tui(self):
        dashboard = terminal.DashboardData(
            digital=terminal.TwinData(elapsed_seconds=3661, preset_seconds=125)
        )
        view = self.model.update(make_frame(dashboard))
        self.assertEqual(view.digital.elapsed_time, "01:01:01")
        self.assertEqual(view.digital.preset_time, "00:02:05")

    def test_idle_clock_stays_at_zero_without_a_run_id(self):
        first = self.model.update(make_frame())
        self.clock.now = 900.0
        later = self.model.update(make_frame())
        self.assertEqual(first.status.test_time_elapsed, "00:00:00")
        self.assertEqual(later.status.test_time_elapsed, "00:00:00")

    def test_gpio_drive_data_is_carried_into_the_view(self):
        gpio = terminal.GpioDriveData(
            pb1_pin=4, pb2_pin=17, pb3_pin=27, pb1_active=True
        )
        dashboard = terminal.DashboardData(gpio_drive=gpio)

        view = self.model.update(make_frame(dashboard))

        self.assertIs(view.gpio_drive, gpio)


class RenderingTests(unittest.TestCase):
    def setUp(self):
        model = terminal.DashboardModel(FakeClock())
        self.view = model.update(make_frame())

    def test_minimum_size_draws_fixed_width_difference_panel(self):
        screen = FakeWindow(terminal.MIN_HEIGHT, terminal.MIN_WIDTH)
        with patch.object(terminal.curses, "doupdate"):
            fitted = terminal.draw_dashboard(screen, self.view)

        self.assertTrue(fitted)
        self.assertEqual(len(screen.children), 7)
        difference = screen.children[1]
        self.assertEqual(difference.width, terminal.DIFFERENCE_WIDTH)
        self.assertEqual(difference.origin[0], 0)

        gpio_drive = screen.children[3]
        digital_uart = screen.children[4]
        hardware_uart = screen.children[5]
        status = screen.children[6]
        expected_uart_y = (
            terminal.TOP_PANEL_HEIGHT + 1 + terminal.GPIO_PANEL_HEIGHT + 1
        )
        self.assertEqual(gpio_drive.origin, (terminal.TOP_PANEL_HEIGHT + 1, 0))
        self.assertEqual(gpio_drive.height, terminal.GPIO_PANEL_HEIGHT)
        self.assertEqual(digital_uart.origin, (expected_uart_y, 0))
        self.assertEqual(hardware_uart.origin[0], expected_uart_y)
        self.assertEqual(digital_uart.height, 8)
        self.assertEqual(
            digital_uart.width + hardware_uart.width + 1,
            terminal.MIN_WIDTH,
        )
        self.assertEqual(status.height, terminal.TOP_PANEL_HEIGHT)
        self.assertEqual(screen.refresh_trace[0], "root")
        self.assertNotIn("root", screen.refresh_trace[1:])
        status_text = " ".join(text for _, _, text in status.text)
        self.assertIn("^R Restart", status_text)
        self.assertIn("^N Next", status_text)
        self.assertIn("^Q Quit", status_text)

    def test_gpio_panel_displays_physical_levels_and_commands(self):
        dashboard = terminal.DashboardData(
            gpio_drive=terminal.GpioDriveData(
                pb1_pin=4,
                pb2_pin=17,
                pb3_pin=27,
                pb1_active=True,
                active_high=True,
            )
        )
        view = terminal.DashboardModel(FakeClock()).update(make_frame(dashboard))
        screen = FakeWindow(terminal.MIN_HEIGHT, terminal.MIN_WIDTH)

        with patch.object(terminal.curses, "doupdate"):
            terminal.draw_dashboard(screen, view)

        gpio_text = " ".join(text for _, _, text in screen.children[3].text)
        self.assertIn("PB1 GPIO4: HIGH (PRESSED)", gpio_text)
        self.assertIn("PB2 GPIO17: LOW (RELEASED)", gpio_text)

    def test_consecutive_frames_redraw_changed_twin_values(self):
        screen = FakeWindow(terminal.MIN_HEIGHT, terminal.MIN_WIDTH)
        changed = terminal.DashboardData(
            digital=terminal.TwinData(
                state="RUN", elapsed_seconds=9, preset_seconds=12, pb1="ON"
            ),
            hardware=terminal.TwinData(
                state="PAUSE", elapsed_seconds=8, preset_seconds=12, pb3="ON"
            ),
        )
        model = terminal.DashboardModel(FakeClock())

        with patch.object(terminal.curses, "doupdate"):
            terminal.draw_dashboard(screen, model.update(make_frame()),)
            terminal.draw_dashboard(screen, model.update(make_frame(changed)),)

        second_frame = screen.children[-7:]
        digital_text = " ".join(text for _, _, text in second_frame[0].text)
        hardware_text = " ".join(text for _, _, text in second_frame[2].text)
        self.assertIn("State   : RUN", digital_text)
        self.assertIn("Elapsed : 00:00:09", digital_text)
        self.assertIn("Preset  : 00:00:12", digital_text)
        self.assertIn("PB1     : ON", digital_text)
        self.assertIn("State   : PAUSE", hardware_text)
        self.assertIn("PB3     : ON", hardware_text)

    def test_small_terminal_shows_warning_without_building_windows(self):
        screen = FakeWindow(10, 40)
        fitted = terminal.draw_dashboard(screen, self.view)
        self.assertFalse(fitted)
        self.assertTrue(screen.refreshed)
        self.assertEqual(screen.children, [])
        self.assertIn("Terminal must be at least", screen.text[0][2])


class TerminalThreadTests(unittest.TestCase):
    def test_hotkeys_enqueue_commands_and_quit_thread(self):
        shared = terminal.TuiSharedState()
        screen = FakeWindow(30, 100, keys=[18, 18, 14, 17])

        def wrapper(callback):
            callback(screen)

        tui = terminal.TerminalTUI(shared, curses_wrapper=wrapper)
        with (
            patch.object(terminal.curses, "raw") as raw,
            patch.object(terminal.curses, "noraw") as noraw,
            patch.object(terminal.curses, "curs_set"),
            patch.object(terminal, "draw_dashboard", return_value=True),
        ):
            tui.start()
            tui.join(1.0)

        self.assertFalse(tui.is_alive)
        self.assertIsNone(tui.error)
        self.assertTrue(screen.keypad_enabled)
        self.assertEqual(screen.timeout_ms, terminal.REFRESH_INTERVAL_MS)
        raw.assert_called_once_with()
        noraw.assert_called_once_with()
        self.assertEqual(
            tuple(shared.get_command() for _ in range(4)),
            (
                terminal.TuiCommand.RESTART_TEST,
                terminal.TuiCommand.RESTART_TEST,
                terminal.TuiCommand.NEXT_TEST,
                terminal.TuiCommand.QUIT,
            ),
        )

    def test_wrapper_failure_is_available_to_controller(self):
        shared = terminal.TuiSharedState()

        def broken_wrapper(_callback):
            raise RuntimeError("terminal unavailable")

        tui = terminal.TerminalTUI(shared, curses_wrapper=broken_wrapper)
        tui.start()
        tui.join(1.0)
        self.assertFalse(tui.is_alive)
        self.assertIsInstance(tui.error, RuntimeError)


if __name__ == "__main__":
    unittest.main()
