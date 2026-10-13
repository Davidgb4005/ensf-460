"""Hardware twin with direct ownership of UART and Raspberry Pi GPIO."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import threading
import time

from testing.harness.models import (
    ButtonInputs,
    FieldChange,
    TwinEvent,
    TwinEventKind,
    TwinSnapshot,
    TwinSource,
)

from .digital_twin import _snapshot_changes
from .protocol import FrameDecoder, StatusFrame, format_frame


@dataclass(frozen=True)
class HardwareTwinConfig:
    uart_port: str
    baud_rate: int = 115200
    gpio_chip: int = 0
    pb1_drive_pin: int = 4
    pb2_drive_pin: int = 17
    pb3_drive_pin: int = 27
    led1_sample_pin: int = 25
    led2_sample_pin: int = 26
    active_high: bool = True
    poll_interval_seconds: float = 0.005

    def __post_init__(self) -> None:
        if not self.uart_port:
            raise ValueError("uart_port is required")
        if self.baud_rate <= 0:
            raise ValueError("baud_rate must be greater than zero")
        if self.poll_interval_seconds <= 0:
            raise ValueError("poll_interval_seconds must be greater than zero")


class HardwareTwin:
    """Drive buttons with GPIO and build hardware state from UART plus LED pins."""

    source = TwinSource.HARDWARE

    def __init__(
        self,
        config: HardwareTwinConfig,
        *,
        gpio_module=None,
        serial_factory=None,
        clock_ns: Callable[[], int] = time.monotonic_ns,
    ) -> None:
        self.config = config
        self._gpio = gpio_module
        self._serial_factory = serial_factory
        self._clock_ns = clock_ns
        self._event_sink = None
        self._serial = None
        self._gpio_handle = None
        self._claimed_pins: list[int] = []
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._lock = threading.RLock()
        self._decoder = FrameDecoder()
        self._sequence = 0
        self._snapshot = TwinSnapshot(
            captured_at_ns=self._clock_ns(),
            state="NO_STATE_SELECTED",
            previous_state="NO_STATE_SELECTED",
            elapsed_seconds=0,
            preset_seconds=0,
            pb1=False,
            pb2=False,
            pb3=False,
            led1_state="INVALID",
            led1_output=False,
            led2_state="INVALID",
            led2_output=False,
        )

    def start(self, event_sink: Callable[[TwinEvent], None]) -> None:
        with self._lock:
            if self._thread is not None:
                raise RuntimeError("hardware twin is already running")
            self._event_sink = event_sink
            self._stop_event.clear()
            try:
                self._open_gpio()
                self._open_serial()
                self._snapshot = self._with_sampled_led_outputs(self._snapshot)
            except BaseException:
                self._close_resources()
                raise
            self._emit(TwinEventKind.STARTED, self._snapshot)
            self._thread = threading.Thread(
                target=self._poll_loop,
                name="hardware-twin",
                daemon=True,
            )
            self._thread.start()

    def set_inputs(self, inputs: ButtonInputs) -> None:
        with self._lock:
            if self._gpio_handle is None:
                raise RuntimeError("hardware twin is not running")
            self._write_pin(self.config.pb1_drive_pin, inputs.pb1)
            self._write_pin(self.config.pb2_drive_pin, inputs.pb2)
            self._write_pin(self.config.pb3_drive_pin, inputs.pb3)

    def snapshot(self) -> TwinSnapshot:
        with self._lock:
            return self._snapshot

    def stop(self) -> None:
        with self._lock:
            thread = self._thread
            if thread is None:
                self._close_resources()
                return
            self._stop_event.set()
        thread.join(timeout=2.0)
        with self._lock:
            self._drive_all_buttons_low()
            self._thread = None
            self._emit(TwinEventKind.STOPPED, self._snapshot)
            self._close_resources()

    def _poll_loop(self) -> None:
        try:
            while not self._stop_event.is_set():
                self._read_uart()
                self._sample_led_outputs()
                self._stop_event.wait(self.config.poll_interval_seconds)
        except BaseException as exc:
            with self._lock:
                self._emit(TwinEventKind.ERROR, self._snapshot, error=str(exc))
            self._stop_event.set()

    def _read_uart(self) -> None:
        waiting = int(getattr(self._serial, "in_waiting", 0))
        data = self._serial.read(waiting or 1)
        if not data:
            return
        for frame in self._decoder.feed(bytes(data)):
            self._apply_frame(frame)

    def _apply_frame(self, frame: StatusFrame) -> None:
        with self._lock:
            previous = self._snapshot
            current = TwinSnapshot(
                captured_at_ns=self._clock_ns(),
                state=frame.state,
                previous_state=frame.previous_state,
                elapsed_seconds=frame.elapsed_seconds,
                preset_seconds=frame.preset_seconds,
                pb1=frame.pb1,
                pb2=frame.pb2,
                pb3=frame.pb3,
                led1_state=frame.led1_state,
                led1_output=self._read_pin(self.config.led1_sample_pin),
                led2_state=frame.led2_state,
                led2_output=self._read_pin(self.config.led2_sample_pin),
            )
            self._snapshot = current
            changes = _snapshot_changes(previous, current)
            if changes:
                self._emit(TwinEventKind.STATE_CHANGED, current, changes=changes)
            self._emit(
                TwinEventKind.UART_MESSAGE,
                current,
                uart_message=format_frame(frame),
            )

    def _sample_led_outputs(self) -> None:
        with self._lock:
            previous = self._snapshot
            current = self._with_sampled_led_outputs(previous)
            changes = _snapshot_changes(previous, current)
            if changes:
                self._snapshot = current
                self._emit(TwinEventKind.STATE_CHANGED, current, changes=changes)

    def _with_sampled_led_outputs(self, snapshot: TwinSnapshot) -> TwinSnapshot:
        return TwinSnapshot(
            captured_at_ns=self._clock_ns(),
            state=snapshot.state,
            previous_state=snapshot.previous_state,
            elapsed_seconds=snapshot.elapsed_seconds,
            preset_seconds=snapshot.preset_seconds,
            pb1=snapshot.pb1,
            pb2=snapshot.pb2,
            pb3=snapshot.pb3,
            led1_state=snapshot.led1_state,
            led1_output=self._read_pin(self.config.led1_sample_pin),
            led2_state=snapshot.led2_state,
            led2_output=self._read_pin(self.config.led2_sample_pin),
        )

    def _open_gpio(self) -> None:
        if self._gpio is None:
            try:
                import lgpio
            except ImportError as exc:
                raise RuntimeError("hardware twin requires the lgpio package") from exc
            self._gpio = lgpio

        self._gpio_handle = self._gpio.gpiochip_open(self.config.gpio_chip)
        for pin in (
            self.config.pb1_drive_pin,
            self.config.pb2_drive_pin,
            self.config.pb3_drive_pin,
        ):
            self._gpio.gpio_claim_output(
                self._gpio_handle, pin, self._logical_level(False)
            )
            self._claimed_pins.append(pin)
        for pin in (self.config.led1_sample_pin, self.config.led2_sample_pin):
            self._gpio.gpio_claim_input(self._gpio_handle, pin)
            self._claimed_pins.append(pin)

    def _open_serial(self) -> None:
        factory = self._serial_factory
        if factory is None:
            try:
                import serial
            except ImportError as exc:
                raise RuntimeError("hardware twin requires the pyserial package") from exc
            factory = serial.Serial
        self._serial = factory(
            self.config.uart_port,
            baudrate=self.config.baud_rate,
            timeout=0,
        )

    def _write_pin(self, pin: int, active: bool) -> None:
        self._gpio.gpio_write(
            self._gpio_handle, pin, self._logical_level(active)
        )

    def _read_pin(self, pin: int) -> bool:
        level = bool(self._gpio.gpio_read(self._gpio_handle, pin))
        return level if self.config.active_high else not level

    def _logical_level(self, active: bool) -> int:
        return int(active if self.config.active_high else not active)

    def _drive_all_buttons_low(self) -> None:
        if self._gpio_handle is None:
            return
        for pin in (
            self.config.pb1_drive_pin,
            self.config.pb2_drive_pin,
            self.config.pb3_drive_pin,
        ):
            try:
                self._write_pin(pin, False)
            except Exception:
                pass

    def _close_resources(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None
        if self._gpio_handle is not None:
            self._drive_all_buttons_low()
            for pin in self._claimed_pins:
                try:
                    self._gpio.gpio_free(self._gpio_handle, pin)
                except Exception:
                    pass
            self._claimed_pins.clear()
            try:
                self._gpio.gpiochip_close(self._gpio_handle)
            finally:
                self._gpio_handle = None

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
