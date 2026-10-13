from enum import Enum
import threading
import time
import unittest

from testing.harness.models import ButtonInputs, TwinEventKind, TwinSource
from testing.twin import (
    DigitalTwin,
    HardwareTwin,
    HardwareTwinConfig,
    StatusFrame,
    encode_frame,
)


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


class FakeButton:
    def __init__(self):
        self.state = GpioState.OFF
        self.last_edge = EdgeType.CONSUMED_EDGE
        self.state_duration = 0
        self.timer_start = time.perf_counter()


class FakeLed:
    def __init__(self):
        self.state = GpioState.OFF
        self.output = GpioState.OFF


class FakeStopwatch:
    def __init__(self):
        self.state = TimerState.TIMER_IDLE
        self.pb_1 = FakeButton()
        self.pb_2 = FakeButton()
        self.pb_3 = FakeButton()
        self.led_1 = FakeLed()
        self.led_2 = FakeLed()
        self.preset_timer = 0
        self.elapsed_timer = 0
        self.main_calls = 0

    def main(self):
        self.main_calls += 1


class FakeGpio:
    def __init__(self):
        self.levels = {}
        self.claimed_outputs = []
        self.claimed_inputs = []
        self.freed = []
        self.closed_handles = []

    def gpiochip_open(self, chip):
        return chip + 100

    def gpio_claim_output(self, handle, pin, initial):
        self.claimed_outputs.append((handle, pin))
        self.levels[pin] = initial

    def gpio_claim_input(self, handle, pin):
        self.claimed_inputs.append((handle, pin))
        self.levels.setdefault(pin, 0)

    def gpio_write(self, handle, pin, level):
        self.levels[pin] = level

    def gpio_read(self, handle, pin):
        return self.levels[pin]

    def gpio_free(self, handle, pin):
        self.freed.append((handle, pin))

    def gpiochip_close(self, handle):
        self.closed_handles.append(handle)


class FakeSerial:
    def __init__(self, port, *, baudrate, timeout):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.closed = False
        self._buffer = bytearray()
        self._lock = threading.Lock()

    @property
    def in_waiting(self):
        with self._lock:
            return len(self._buffer)

    def append(self, data):
        with self._lock:
            self._buffer.extend(data)

    def read(self, size):
        with self._lock:
            data = bytes(self._buffer[:size])
            del self._buffer[:size]
            return data

    def close(self):
        self.closed = True


def wait_until(predicate, timeout=0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.001)
    raise AssertionError("condition was not reached before timeout")


class DigitalTwinTests(unittest.TestCase):
    def test_wraps_model_without_replacing_main_logic(self):
        model = FakeStopwatch()
        events = []
        twin = DigitalTwin(model, poll_interval_seconds=0.001)

        twin.start(events.append)
        twin.set_inputs(ButtonInputs(pb1=True, pb3=True))
        snapshot = twin.snapshot()
        pressed_edge = model.pb_1.last_edge
        twin.stop()

        self.assertEqual(model.main_calls, 1)
        self.assertTrue(snapshot.pb1)
        self.assertFalse(snapshot.pb2)
        self.assertTrue(snapshot.pb3)
        self.assertEqual(pressed_edge, EdgeType.RISING_EDGE)
        self.assertEqual(model.pb_1.last_edge, EdgeType.FALLING_EDGE)
        self.assertEqual(events[0].kind, TwinEventKind.STARTED)
        self.assertTrue(any(event.kind is TwinEventKind.UART_MESSAGE for event in events))
        self.assertTrue(all(event.source is TwinSource.DIGITAL for event in events))

    def test_observes_timer_and_led_changes(self):
        model = FakeStopwatch()
        events = []
        twin = DigitalTwin(model, poll_interval_seconds=0.001)
        twin.start(events.append)

        model.state = TimerState.TIMER_RUN
        model.preset_timer = 10
        model.elapsed_timer = 9
        model.led_1.state = GpioState.BLINK
        model.led_1.output = GpioState.ON
        wait_until(lambda: twin.snapshot().state == "RUN")
        wait_until(
            lambda: any(event.kind is TwinEventKind.STATE_CHANGED for event in events)
        )
        snapshot = twin.snapshot()
        twin.stop()

        self.assertEqual(snapshot.previous_state, "IDLE")
        self.assertEqual(snapshot.preset_seconds, 10)
        self.assertEqual(snapshot.elapsed_seconds, 9)
        self.assertEqual(snapshot.led1_state, "BLINK")
        self.assertTrue(snapshot.led1_output)


class HardwareTwinTests(unittest.TestCase):
    def make_twin(self, *, active_high=True):
        gpio = FakeGpio()
        serial_instances = []

        def serial_factory(*args, **kwargs):
            instance = FakeSerial(*args, **kwargs)
            serial_instances.append(instance)
            return instance

        config = HardwareTwinConfig(
            uart_port="/dev/fake-uart",
            active_high=active_high,
            poll_interval_seconds=0.001,
        )
        twin = HardwareTwin(
            config,
            gpio_module=gpio,
            serial_factory=serial_factory,
        )
        return twin, gpio, serial_instances

    def test_uart_builds_snapshot_and_gpio_supplies_led_outputs(self):
        twin, gpio, serial_instances = self.make_twin()
        events = []
        twin.start(events.append)
        gpio.levels[25] = 1
        gpio.levels[26] = 0
        serial_instances[0].append(
            encode_frame(
                StatusFrame(
                    elapsed_seconds=58,
                    preset_seconds=60,
                    state_code=1,
                    previous_state_code=0,
                    pb1=True,
                    pb2=False,
                    pb3=True,
                    led1_state_code=3,
                    led2_state_code=2,
                )
            )
        )

        wait_until(lambda: twin.snapshot().state == "RUN")
        snapshot = twin.snapshot()
        twin.stop()

        self.assertEqual(snapshot.previous_state, "IDLE")
        self.assertEqual(snapshot.elapsed_seconds, 58)
        self.assertEqual(snapshot.preset_seconds, 60)
        self.assertEqual((snapshot.pb1, snapshot.pb2, snapshot.pb3), (True, False, True))
        self.assertEqual(snapshot.led1_state, "BLINK")
        self.assertTrue(snapshot.led1_output)
        self.assertEqual(snapshot.led2_state, "OFF")
        self.assertFalse(snapshot.led2_output)
        self.assertTrue(any(event.kind is TwinEventKind.UART_MESSAGE for event in events))

    def test_set_inputs_drives_gpio_and_stop_releases_resources(self):
        twin, gpio, serial_instances = self.make_twin()
        twin.start(lambda event: None)
        twin.set_inputs(ButtonInputs(pb1=True, pb2=False, pb3=True))

        self.assertEqual((gpio.levels[4], gpio.levels[17], gpio.levels[27]), (1, 0, 1))
        twin.stop()

        self.assertEqual((gpio.levels[4], gpio.levels[17], gpio.levels[27]), (0, 0, 0))
        self.assertTrue(serial_instances[0].closed)
        self.assertEqual(len(gpio.freed), 5)
        self.assertEqual(gpio.closed_handles, [100])

    def test_active_low_gpio_is_inverted(self):
        twin, gpio, _ = self.make_twin(active_high=False)
        twin.start(lambda event: None)
        twin.set_inputs(ButtonInputs(pb1=True))

        self.assertEqual(gpio.levels[4], 0)
        self.assertEqual(gpio.levels[17], 1)
        gpio.levels[25] = 0
        wait_until(lambda: twin.snapshot().led1_output)
        twin.stop()

    def test_uart_failure_is_reported_as_twin_error(self):
        class BrokenSerial(FakeSerial):
            def read(self, size):
                raise OSError("UART disconnected")

        gpio = FakeGpio()
        events = []
        twin = HardwareTwin(
            HardwareTwinConfig(
                uart_port="broken",
                poll_interval_seconds=0.001,
            ),
            gpio_module=gpio,
            serial_factory=BrokenSerial,
        )
        twin.start(events.append)
        wait_until(lambda: any(event.kind is TwinEventKind.ERROR for event in events))
        twin.stop()

        error = next(event for event in events if event.kind is TwinEventKind.ERROR)
        self.assertIn("UART disconnected", error.error)


if __name__ == "__main__":
    unittest.main()
