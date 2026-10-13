"""Configuration and enums used by the rebuilt twins."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum


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


class EdgeType(Enum):
    CONSUMED_EDGE = 0
    RISING_EDGE = 1
    FALLING_EDGE = 2


@dataclass(frozen=True)
class TimingConfig:
    fast_increment_count: int = 20
    debounce_delay_s: float = 0.060
    increment_delay_s: float = 0.200
    reset_delay_s: float = 3.000
    clear_delay_s: float = 3.000
    count_delay_s: float = 1.000
    led1_blink_delay_s: float = 1.000
    led2_blink_delay_s: float = 0.250
    twin_poll_s: float = 0.005


@dataclass(frozen=True)
class PresetConfig:
    max_preset_s: int = 3599
    min_preset_s: int = 0
    short_second_step_s: int = 1
    long_second_step_s: int = 5
    short_minute_step_s: int = 60
    long_minute_step_s: int = 300


@dataclass(frozen=True)
class GpioConfig:
    gpio_chip: int = 0
    pb1_drive_pin: int = 4
    pb2_drive_pin: int = 17
    pb3_drive_pin: int = 27
    led1_sample_pin: int = 25
    led2_sample_pin: int = 26
    active_high: bool = True


@dataclass(frozen=True)
class UartConfig:
    # /dev/serial0 follows whichever hardware UART is routed to GPIO14/15.
    port: str = "/dev/serial0"
    baud_rate: int = 9600


@dataclass(frozen=True)
class TwinConfig:
    timing: TimingConfig = field(default_factory=TimingConfig)
    preset: PresetConfig = field(default_factory=PresetConfig)
    gpio: GpioConfig = field(default_factory=GpioConfig)
    uart: UartConfig = field(default_factory=UartConfig)


DEFAULT_CONFIG = TwinConfig()
