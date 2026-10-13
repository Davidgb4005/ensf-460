"""Digital and hardware implementations of the harness twin interface."""

from .config import (
    DEFAULT_CONFIG,
    EdgeType,
    GpioConfig,
    GpioState,
    PresetConfig,
    TimerState,
    TimingConfig,
    TwinConfig,
)
from .digital_twin import DigitalTwin
from .hardware_twin import HardwareTwin, HardwareTwinConfig
from .protocol import (
    FRAME_MARKER,
    FRAME_SIZE,
    FrameDecoder,
    StatusFrame,
    decode_frame,
    encode_frame,
)
from .timer_model import Led, PushButton, StopWatch, build_digital_model

__all__ = (
    "DEFAULT_CONFIG",
    "DigitalTwin",
    "EdgeType",
    "FRAME_MARKER",
    "FRAME_SIZE",
    "FrameDecoder",
    "GpioConfig",
    "GpioState",
    "HardwareTwin",
    "HardwareTwinConfig",
    "Led",
    "PresetConfig",
    "PushButton",
    "StatusFrame",
    "StopWatch",
    "TimerState",
    "TimingConfig",
    "TwinConfig",
    "build_digital_model",
    "decode_frame",
    "encode_frame",
)
