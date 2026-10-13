"""Threaded curses dashboard for the digital-twin test rig.

The TUI deliberately knows nothing about the test runner, twins, UART devices, or
log files. Producers publish immutable snapshots and already-timestamped UART
messages to :class:`TuiSharedState`; the UI renders them and emits commands.
"""

from __future__ import annotations

import curses
from collections import deque
from dataclasses import dataclass, field
from enum import Enum, auto
from queue import Empty, Queue
import threading
import time
from typing import Callable


MIN_WIDTH = 100
MIN_HEIGHT = 30
REFRESH_INTERVAL_MS = 50
DIFFERENCE_WIDTH = 12
TOP_PANEL_HEIGHT = 15
GPIO_PANEL_HEIGHT = 4
MAX_UART_BACKLOG = 1_000
_NO_RUN_SEEN = object()


class TuiCommand(Enum):
    """Commands that the controller can receive from the TUI."""

    RESTART_TEST = auto()
    NEXT_TEST = auto()
    QUIT = auto()


@dataclass(frozen=True)
class TwinData:
    """Raw values for one twin; formatting is owned by the TUI."""

    state: str = "IDLE"
    previous_state: str = "IDLE"
    elapsed_seconds: int = 0
    preset_seconds: int = 0
    pb1: str = "OFF"
    pb2: str = "OFF"
    pb3: str = "OFF"


@dataclass(frozen=True)
class TestData:
    """Metadata for the current and next automated tests."""

    run_id: int | None = None
    current_name: str = ""
    current_path: str = ""
    next_name: str = ""
    next_path: str = ""
    duration_seconds: float = 0.0
    status: str = "IDLE"


@dataclass(frozen=True)
class OutputData:
    """External resources displayed in the lower status panel."""

    hardware_uart_path: str = ""
    digital_uart_path: str = ""
    power_profiler_status: str = "DISCONNECTED"
    hardware_uart_port: str = ""


@dataclass(frozen=True)
class GpioDriveData:
    """GPIO pins and the logical button levels commanded by the harness."""

    pb1_pin: int | None = None
    pb2_pin: int | None = None
    pb3_pin: int | None = None
    pb1_active: bool = False
    pb2_active: bool = False
    pb3_active: bool = False
    active_high: bool = True


@dataclass(frozen=True)
class DashboardData:
    """One complete, immutable display snapshot."""

    digital: TwinData = field(default_factory=TwinData)
    hardware: TwinData = field(default_factory=TwinData)
    test: TestData = field(default_factory=TestData)
    outputs: OutputData = field(default_factory=OutputData)
    gpio_drive: GpioDriveData = field(default_factory=GpioDriveData)


@dataclass(frozen=True)
class TuiFrame:
    """An atomic snapshot plus UART messages not previously read by the TUI."""

    dashboard: DashboardData
    digital_uart: tuple[str, ...]
    hardware_uart: tuple[str, ...]


class TuiSharedState:
    """The sole object shared between the controller/logger and the TUI.

    Snapshot replacement and UART inbox draining are atomic. The complete UART
    record belongs in the logger's files; these inboxes are only a live-display
    transport.
    """

    def __init__(self, initial: DashboardData | None = None) -> None:
        self._lock = threading.Lock()
        self._dashboard = initial or DashboardData()
        self._digital_uart: deque[str] = deque(maxlen=MAX_UART_BACKLOG)
        self._hardware_uart: deque[str] = deque(maxlen=MAX_UART_BACKLOG)
        self.command_queue: Queue[TuiCommand] = Queue()

    def publish(self, dashboard: DashboardData) -> None:
        """Atomically replace the full dashboard snapshot."""

        if not isinstance(dashboard, DashboardData):
            raise TypeError("dashboard must be a DashboardData instance")
        with self._lock:
            self._dashboard = dashboard

    def append_digital_uart(self, message: str) -> None:
        self._append_uart(self._digital_uart, message)

    def append_hardware_uart(self, message: str) -> None:
        self._append_uart(self._hardware_uart, message)

    def _append_uart(self, inbox: deque[str], message: str) -> None:
        if not isinstance(message, str):
            raise TypeError("UART message must be a string")
        with self._lock:
            inbox.append(message)

    def read_frame(self) -> TuiFrame:
        """Copy the current snapshot and consume pending display messages."""

        with self._lock:
            frame = TuiFrame(
                dashboard=self._dashboard,
                digital_uart=tuple(self._digital_uart),
                hardware_uart=tuple(self._hardware_uart),
            )
            self._digital_uart.clear()
            self._hardware_uart.clear()
            return frame

    def get_command(
        self, *, block: bool = False, timeout: float | None = None
    ) -> TuiCommand:
        """Return the next UI command, raising ``queue.Empty`` if unavailable."""

        return self.command_queue.get(block=block, timeout=timeout)


@dataclass(frozen=True)
class TwinView:
    state: str
    previous_state: str
    elapsed_time: str
    preset_time: str
    pb1: str
    pb2: str
    pb3: str


@dataclass(frozen=True)
class StatusView:
    test_time_elapsed: str
    time_remaining: str
    test_status: str
    current_test_name: str
    current_test_path: str
    next_test_name: str
    next_test_path: str
    hardware_uart_path: str
    digital_uart_path: str
    power_profiler_status: str
    hardware_uart_port: str


@dataclass(frozen=True)
class DashboardView:
    digital: TwinView
    hardware: TwinView
    differences: dict[str, str]
    status: StatusView
    gpio_drive: GpioDriveData
    digital_uart: tuple[str, ...]
    hardware_uart: tuple[str, ...]


COMPARED_FIELDS = (
    "state",
    "previous_state",
    "elapsed_seconds",
    "preset_seconds",
    "pb1",
    "pb2",
    "pb3",
)


class DashboardModel:
    """Presentation state derived from raw snapshots using a monotonic clock."""

    def __init__(self, clock: Callable[[], float] = time.monotonic) -> None:
        self._clock = clock
        self._run_id: int | None | object = _NO_RUN_SEEN
        self._run_started_at = 0.0
        self._mismatch_started: dict[str, float | None] = {
            name: None for name in COMPARED_FIELDS
        }
        self._digital_uart: deque[str] = deque()
        self._hardware_uart: deque[str] = deque()

    def update(
        self,
        frame: TuiFrame,
        *,
        serial_capacity: int | None = None,
    ) -> DashboardView:
        now = self._clock()
        data = frame.dashboard

        if data.test.run_id != self._run_id:
            self._run_id = data.test.run_id
            self._run_started_at = now
            self._mismatch_started = {name: None for name in COMPARED_FIELDS}

        differences: dict[str, str] = {}
        for name in COMPARED_FIELDS:
            matches = getattr(data.digital, name) == getattr(data.hardware, name)
            started = self._mismatch_started[name]
            if matches:
                self._mismatch_started[name] = None
                differences[name] = "0ms"
            else:
                if started is None:
                    started = now
                    self._mismatch_started[name] = started
                differences[name] = f"{max(0, int((now - started) * 1000))}ms"

        self._ingest_uart(self._digital_uart, frame.digital_uart)
        self._ingest_uart(self._hardware_uart, frame.hardware_uart)
        retained_lines = (
            MAX_UART_BACKLOG if serial_capacity is None else serial_capacity
        )
        self._trim_uart(self._digital_uart, retained_lines)
        self._trim_uart(self._hardware_uart, retained_lines)

        elapsed = (
            0.0
            if data.test.run_id is None
            else max(0.0, now - self._run_started_at)
        )
        remaining = max(0.0, data.test.duration_seconds - elapsed)
        return DashboardView(
            digital=self._twin_view(data.digital),
            hardware=self._twin_view(data.hardware),
            differences=differences,
            status=StatusView(
                test_time_elapsed=format_duration(elapsed),
                time_remaining=format_duration(remaining),
                test_status=data.test.status,
                current_test_name=data.test.current_name,
                current_test_path=data.test.current_path,
                next_test_name=data.test.next_name,
                next_test_path=data.test.next_path,
                hardware_uart_path=data.outputs.hardware_uart_path,
                digital_uart_path=data.outputs.digital_uart_path,
                power_profiler_status=data.outputs.power_profiler_status,
                hardware_uart_port=data.outputs.hardware_uart_port,
            ),
            gpio_drive=data.gpio_drive,
            digital_uart=tuple(self._digital_uart),
            hardware_uart=tuple(self._hardware_uart),
        )

    @staticmethod
    def _ingest_uart(buffer: deque[str], messages: tuple[str, ...]) -> None:
        for message in messages:
            buffer.appendleft(message)

    @staticmethod
    def _trim_uart(buffer: deque[str], capacity: int) -> None:
        capacity = max(0, capacity)
        while len(buffer) > capacity:
            buffer.pop()

    @staticmethod
    def _twin_view(data: TwinData) -> TwinView:
        return TwinView(
            state=str(data.state),
            previous_state=str(data.previous_state),
            elapsed_time=format_duration(data.elapsed_seconds),
            preset_time=format_duration(data.preset_seconds),
            pb1=str(data.pb1),
            pb2=str(data.pb2),
            pb3=str(data.pb3),
        )


def format_duration(seconds: float) -> str:
    """Format non-negative seconds as HH:MM:SS for the dashboard."""

    whole_seconds = max(0, int(seconds))
    hours, remainder = divmod(whole_seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"


def _addnstr(win, row: int, column: int, text: object, count: int) -> None:
    """Draw clipped text without letting a terminal edge abort the UI."""

    if count <= 0:
        return
    try:
        win.addnstr(row, column, str(text), count)
    except curses.error:
        pass


def field_line(
    win, row: int, label: str, value: object, *, label_width: int = 14
) -> None:
    width = win.getmaxyx()[1]
    _addnstr(win, row, 2, f"{label:<{label_width}}: {value}", width - 4)


def title_line(win, title: str) -> None:
    width = win.getmaxyx()[1]
    _addnstr(win, 1, max(1, (width - len(title)) // 2), title, width - 2)


def draw_twin(win, title: str, data: TwinView) -> None:
    win.erase()
    win.box()
    title_line(win, title)
    for row, label, value in (
        (3, "State", data.state),
        (4, "Previous", data.previous_state),
        (5, "Elapsed", data.elapsed_time),
        (6, "Preset", data.preset_time),
        (7, "PB1", data.pb1),
        (8, "PB2", data.pb2),
        (9, "PB3", data.pb3),
    ):
        field_line(win, row, label, value, label_width=8)
    win.noutrefresh()


def draw_difference(win, differences: dict[str, str]) -> None:
    win.erase()
    win.box()
    title_line(win, "Difference")
    width = win.getmaxyx()[1]
    for row, name in enumerate(COMPARED_FIELDS, start=3):
        _addnstr(win, row, 2, differences[name], width - 4)
    win.noutrefresh()


def draw_status(win, data: StatusView) -> None:
    win.erase()
    win.box()
    title_line(win, "Status")
    for row, label, value in (
        (2, "Test Status", data.test_status),
        (3, "Time Elapsed", data.test_time_elapsed),
        (4, "Time Remaining", data.time_remaining),
        (5, "Current Test", data.current_test_name),
        (6, "Current Path", data.current_test_path),
        (7, "Next Test", data.next_test_name),
        (8, "Next Path", data.next_test_path),
        (9, "Hardware UART", data.hardware_uart_path),
        (10, "Digital UART", data.digital_uart_path),
        (11, "Power Profiler", data.power_profiler_status),
        (12, "Hardware Port", data.hardware_uart_port),
    ):
        field_line(win, row, label, value)
    width = win.getmaxyx()[1]
    _addnstr(win, 13, 2, "^R Restart  ^N Next  ^Q Quit", width - 4)
    win.noutrefresh()


def draw_serial(win, title: str, lines: tuple[str, ...]) -> None:
    win.erase()
    win.box()
    title_line(win, title)
    height, width = win.getmaxyx()
    for row, line in enumerate(lines[: max(0, height - 4)], start=3):
        _addnstr(win, row, 2, line, width - 4)
    win.noutrefresh()


def draw_gpio_drive(win, data: GpioDriveData) -> None:
    win.erase()
    win.box()
    title_line(win, "Pi GPIO Drive")
    height, width = win.getmaxyx()
    if height > 2:
        entries = []
        for name, pin, active in (
            ("PB1", data.pb1_pin, data.pb1_active),
            ("PB2", data.pb2_pin, data.pb2_active),
            ("PB3", data.pb3_pin, data.pb3_active),
        ):
            if pin is None:
                entries.append(f"{name}: not configured")
                continue
            physical_high = active if data.active_high else not active
            level = "HIGH" if physical_high else "LOW"
            command = "PRESSED" if active else "RELEASED"
            entries.append(f"{name} GPIO{pin}: {level} ({command})")
        text = "   |   ".join(entries)
        _addnstr(win, 2, 2, text, width - 4)
    win.noutrefresh()


def serial_capacity(height: int) -> int:
    serial_y = TOP_PANEL_HEIGHT + 1 + GPIO_PANEL_HEIGHT + 1
    serial_height = height - serial_y - 1
    return max(0, serial_height - 4)


def draw_dashboard(stdscr, data: DashboardView) -> bool:
    """Draw one frame, returning whether the full dashboard fitted."""

    height, width = stdscr.getmaxyx()
    stdscr.erase()
    if width < MIN_WIDTH or height < MIN_HEIGHT:
        message = (
            f"Terminal must be at least {MIN_WIDTH}x{MIN_HEIGHT} "
            f"(currently {width}x{height})"
        )
        _addnstr(stdscr, 0, 0, message, max(0, width - 1))
        stdscr.refresh()
        return False

    # Derived windows share the parent screen. Queue the cleared parent before
    # drawing its children; refreshing the parent last can overwrite changed
    # child contents on real ncurses implementations.
    stdscr.noutrefresh()

    gap = 1
    left_width = width * 62 // 100
    right_x = left_width + gap
    right_width = width - right_x - gap
    top_height = TOP_PANEL_HEIGHT
    gpio_y = top_height + gap
    serial_y = gpio_y + GPIO_PANEL_HEIGHT + gap
    serial_height = height - serial_y - gap

    diff_width = DIFFERENCE_WIDTH
    twin_width = (left_width - diff_width - 2 * gap) // 2
    hardware_x = twin_width + gap + diff_width + gap
    hardware_width = left_width - hardware_x

    digital = stdscr.derwin(top_height, twin_width, 0, 0)
    difference = stdscr.derwin(top_height, diff_width, 0, twin_width + gap)
    hardware = stdscr.derwin(top_height, hardware_width, 0, hardware_x)
    gpio_drive = stdscr.derwin(GPIO_PANEL_HEIGHT, width - gap, gpio_y, 0)

    serial_width = (width - gap) // 2
    digital_serial = stdscr.derwin(serial_height, serial_width, serial_y, 0)
    hardware_serial = stdscr.derwin(
        serial_height,
        width - serial_width - gap,
        serial_y,
        serial_width + gap,
    )
    status = stdscr.derwin(top_height, right_width, 0, right_x)

    draw_twin(digital, "Digital Twin", data.digital)
    draw_difference(difference, data.differences)
    draw_twin(hardware, "Hardware Twin", data.hardware)
    draw_gpio_drive(gpio_drive, data.gpio_drive)
    draw_serial(digital_serial, "Digital UART Output", data.digital_uart)
    draw_serial(hardware_serial, "Hardware UART Output", data.hardware_uart)
    draw_status(status, data.status)

    curses.doupdate()
    return True


class TerminalTUI:
    """Own and run the curses UI in a background thread."""

    def __init__(
        self,
        shared_state: TuiSharedState,
        *,
        clock: Callable[[], float] = time.monotonic,
        curses_wrapper: Callable[[Callable], object] = curses.wrapper,
    ) -> None:
        if not isinstance(shared_state, TuiSharedState):
            raise TypeError("shared_state must be a TuiSharedState instance")
        self._shared_state = shared_state
        self._model = DashboardModel(clock)
        self._curses_wrapper = curses_wrapper
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._error: BaseException | None = None

    @property
    def error(self) -> BaseException | None:
        return self._error

    @property
    def is_alive(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self) -> None:
        if self.is_alive:
            raise RuntimeError("TUI is already running")
        self._stop_event.clear()
        self._error = None
        self._thread = threading.Thread(
            target=self._run,
            name="terminal-tui",
            daemon=True,
        )
        self._thread.start()

    def stop(self) -> None:
        """Request a clean UI shutdown without emitting a QUIT command."""

        self._stop_event.set()

    def join(self, timeout: float | None = None) -> None:
        if self._thread is not None:
            self._thread.join(timeout)

    def _run(self) -> None:
        try:
            self._curses_wrapper(self._curses_main)
        except BaseException as exc:  # Preserve failures for the owning controller.
            self._error = exc
        finally:
            self._stop_event.set()

    def _curses_main(self, stdscr) -> None:
        raw_mode = False
        try:
            curses.raw()
            raw_mode = True
        except curses.error:
            pass
        try:
            try:
                curses.curs_set(0)
            except curses.error:
                pass
            stdscr.keypad(True)
            stdscr.timeout(REFRESH_INTERVAL_MS)

            while not self._stop_event.is_set():
                height, width = stdscr.getmaxyx()
                capacity = (
                    serial_capacity(height)
                    if width >= MIN_WIDTH and height >= MIN_HEIGHT
                    else None
                )
                view = self._model.update(
                    self._shared_state.read_frame(),
                    serial_capacity=capacity,
                )
                draw_dashboard(stdscr, view)

                key = stdscr.getch()
                if key == 18:  # Ctrl+R
                    self._shared_state.command_queue.put(TuiCommand.RESTART_TEST)
                elif key == 14:  # Ctrl+N
                    self._shared_state.command_queue.put(TuiCommand.NEXT_TEST)
                elif key == 17:  # Ctrl+Q
                    self._shared_state.command_queue.put(TuiCommand.QUIT)
                    self._stop_event.set()
        finally:
            if raw_mode:
                curses.noraw()


def main() -> None:
    """Run an empty standalone dashboard for interactive inspection."""

    shared_state = TuiSharedState()
    tui = TerminalTUI(shared_state)
    tui.start()
    try:
        while tui.is_alive:
            try:
                command = shared_state.get_command(block=True, timeout=0.1)
            except Empty:
                continue
            if command is TuiCommand.QUIT:
                break
    except KeyboardInterrupt:
        tui.stop()
    finally:
        tui.stop()
        tui.join()
    if tui.error is not None:
        raise tui.error


if __name__ == "__main__":
    main()
