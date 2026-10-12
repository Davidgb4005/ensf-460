from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


class TimerState(Enum):
    TIMER_NO_STATE_SELECTED = 0
    TIMER_IDLE = 1
    TIMER_RUN = 2
    TIMER_PAUSE = 3
    TIMER_ALARM = 4


class GpioState(Enum):
    ON = 1
    OFF = 2
    BLINK = 3


class StateTransition(Enum):
    CHANGE_ALLOWED = 1
    CHANGE_BLOCKED = 2


class EdgeType(Enum):
    CONSUMED_EDGE = 0
    RISING_EDGE = 1
    FALLING_EDGE = 2


class TestMode(Enum):
    AUTOMATED = 1
    MANUAL = 2


@dataclass
class TimingConfig:
    fast_increment_count: int = 20
    debounce_delay_s: float = 0.060
    increment_delay_s: float = 0.200
    reset_delay_s: float = 3.000
    clear_delay_s: float = 3.000
    count_delay_s: float = 1.000
    led1_blink_delay_s: float = 1.000
    led2_blink_delay_s: float = 0.250
    tui_refresh_s: float = 0.0675
    manual_sample_s: float = 0.010
    log_poll_s: float = 0.005


@dataclass
class PresetConfig:
    max_preset_s: int = 5999
    min_preset_s: int = 0
    short_second_step_s: int = 1
    long_second_step_s: int = 5
    short_minute_step_s: int = 60
    long_minute_step_s: int = 300


@dataclass
class GpioConfig:
    gpio_chip: int | None = 0
    pb1_drive_pin: int | None = 2
    pb1_sample_pin: int | None = 17
    pb2_drive_pin: int | None = 3 
    pb2_sample_pin: int | None = 27
    pb3_drive_pin: int | None = 4
    pb3_sample_pin: int | None = 22
    led1_sample_pin: int | None = 8
    led2_sample_pin: int | None = 10
    state_bit0_pin: int | None = 23
    state_bit1_pin: int | None = 24
    active_high: bool = True


@dataclass
class TestConfig:
    mode: TestMode = TestMode.MANUAL
    command_file: str | None = str(Path(__file__).with_name("input_commands.txt"))
    log_file: str = str(Path(__file__).with_name("twin_events.csv"))
    timing: TimingConfig = field(default_factory=TimingConfig)
    preset: PresetConfig = field(default_factory=PresetConfig)
    gpio: GpioConfig = field(default_factory=GpioConfig)


DEFAULT_COMMANDS = (
    ("press", ("pb1", "4")),
    ("wait", ("1",)),
    ("wait", (".125",)),
    ("press", ("pb2", "1")),
    ("press", ("pb1", "1")),
)


CONFIG = TestConfig()
