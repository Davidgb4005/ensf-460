"""Binary status protocol shared by the firmware and hardware twin."""

from __future__ import annotations

from dataclasses import dataclass
import struct


FRAME_MARKER = b"\xAA\x55"
PAYLOAD_SIZE = 6
FRAME_SIZE = len(FRAME_MARKER) + PAYLOAD_SIZE + 1
_PAYLOAD = struct.Struct("<HHH")


TIMER_STATES = {
    0: "IDLE",
    1: "RUN",
    2: "PAUSE",
    3: "ALARM",
}

LED_STATES = {
    0: "BLINK",
    1: "PWM",
    2: "DIGITAL",
    3: "RESERVED",
}


@dataclass(frozen=True)
class StatusFrame:
    elapsed_seconds: int
    preset_seconds: int
    state_code: int
    previous_state_code: int
    pb1: bool
    pb2: bool
    pb3: bool
    led1_state_code: int
    led2_state_code: int

    @property
    def state(self) -> str:
        return TIMER_STATES.get(self.state_code, f"UNKNOWN_{self.state_code}")

    @property
    def previous_state(self) -> str:
        return TIMER_STATES.get(
            self.previous_state_code, f"UNKNOWN_{self.previous_state_code}"
        )

    @property
    def led1_state(self) -> str:
        return LED_STATES[self.led1_state_code]

    @property
    def led2_state(self) -> str:
        return LED_STATES[self.led2_state_code]


def checksum(payload: bytes) -> int:
    """Return the protocol's unsigned eight-bit additive checksum."""
    return sum(payload) & 0xFF


def encode_frame(frame: StatusFrame) -> bytes:
    """Encode a status frame using the firmware wire layout."""
    _validate_uint16("elapsed_seconds", frame.elapsed_seconds)
    _validate_uint16("preset_seconds", frame.preset_seconds)
    _validate_range("state_code", frame.state_code, 0, 7)
    _validate_range("previous_state_code", frame.previous_state_code, 0, 7)
    _validate_range("led1_state_code", frame.led1_state_code, 0, 3)
    _validate_range("led2_state_code", frame.led2_state_code, 0, 3)

    flags = (
        frame.state_code
        | (frame.previous_state_code << 3)
        | (int(frame.pb1) << 6)
        | (int(frame.pb2) << 7)
        | (int(frame.pb3) << 8)
        | (frame.led1_state_code << 9)
        | (frame.led2_state_code << 11)
    )
    payload = _PAYLOAD.pack(frame.elapsed_seconds, frame.preset_seconds, flags)
    return FRAME_MARKER + payload + bytes((checksum(payload),))


def decode_frame(data: bytes) -> StatusFrame:
    """Decode and validate one complete frame."""
    if len(data) != FRAME_SIZE:
        raise ValueError(f"status frame must be {FRAME_SIZE} bytes")
    if not data.startswith(FRAME_MARKER):
        raise ValueError("invalid status frame marker")

    payload = data[len(FRAME_MARKER) : -1]
    if data[-1] != checksum(payload):
        raise ValueError("invalid status frame checksum")

    elapsed, preset, flags = _PAYLOAD.unpack(payload)
    return StatusFrame(
        elapsed_seconds=elapsed,
        preset_seconds=preset,
        state_code=flags & 0b111,
        previous_state_code=(flags >> 3) & 0b111,
        pb1=bool((flags >> 6) & 1),
        pb2=bool((flags >> 7) & 1),
        pb3=bool((flags >> 8) & 1),
        led1_state_code=(flags >> 9) & 0b11,
        led2_state_code=(flags >> 11) & 0b11,
    )


class FrameDecoder:
    """Recover complete status frames from arbitrary UART byte chunks."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.invalid_frames = 0

    def feed(self, data: bytes) -> tuple[StatusFrame, ...]:
        if not isinstance(data, bytes):
            raise TypeError("UART data must be bytes")
        self._buffer.extend(data)
        frames: list[StatusFrame] = []

        while True:
            marker_index = self._buffer.find(FRAME_MARKER)
            if marker_index < 0:
                # Retain a possible first marker byte split across reads.
                self._buffer[:] = (
                    FRAME_MARKER[:1]
                    if self._buffer.endswith(FRAME_MARKER[:1])
                    else b""
                )
                break
            if marker_index:
                del self._buffer[:marker_index]
            if len(self._buffer) < FRAME_SIZE:
                break

            candidate = bytes(self._buffer[:FRAME_SIZE])
            try:
                frames.append(decode_frame(candidate))
            except ValueError:
                self.invalid_frames += 1
                del self._buffer[0]
            else:
                del self._buffer[:FRAME_SIZE]

        return tuple(frames)


def format_frame(frame: StatusFrame) -> str:
    """Make a decoded binary frame readable in the TUI and text logs."""
    buttons = f"{int(frame.pb1)}{int(frame.pb2)}{int(frame.pb3)}"
    return (
        f"state={frame.state} previous={frame.previous_state} "
        f"elapsed={frame.elapsed_seconds} preset={frame.preset_seconds} "
        f"pb={buttons} led1={frame.led1_state} led2={frame.led2_state}"
    )


def _validate_uint16(name: str, value: int) -> None:
    _validate_range(name, value, 0, 0xFFFF)


def _validate_range(name: str, value: int, minimum: int, maximum: int) -> None:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"{name} must be an integer")
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
