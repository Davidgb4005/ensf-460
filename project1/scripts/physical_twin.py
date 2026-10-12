from testing_config import EdgeType, GpioState, TimerState
from digital_twin import Led, PushButton, StopWatch, duration_timer

try:
    from gpiozero import DigitalInputDevice, DigitalOutputDevice
except ImportError:
    DigitalInputDevice = None
    DigitalOutputDevice = None


def _gpio_state(value):
    return GpioState.ON if value else GpioState.OFF


class GpioInput:
    def __init__(self, pin, active_high=True, pull_up=False):
        self.pin = pin
        self.active_high = active_high
        self._device = None
        if pin is not None and DigitalInputDevice is not None:
            self._device = DigitalInputDevice(
                pin,
                pull_up=pull_up,
                active_state=active_high,
            )

    def read(self):
        if self._device is None:
            return GpioState.OFF
        return _gpio_state(self._device.value)

    def close(self):
        if self._device is not None:
            self._device.close()


class GpioOutput:
    def __init__(self, pin, active_high=True, initial_state=GpioState.OFF):
        self.pin = pin
        self.active_high = active_high
        self._device = None
        if pin is not None and DigitalOutputDevice is not None:
            self._device = DigitalOutputDevice(
                pin,
                active_high=active_high,
                initial_value=(initial_state == GpioState.ON),
            )

    def write(self, state):
        if self._device is not None:
            self._device.value = state == GpioState.ON

    def close(self):
        if self._device is not None:
            self._device.close()


class PhysicalPushButton(PushButton):
    def __init__(
        self,
        name,
        state,
        drive_pin,
        sample_pin,
        active_high=True,
        pull_up=False,
        drive_enabled=True,
    ):
        super().__init__(name, state)
        self.drive_pin = drive_pin
        self.sample_pin = sample_pin
        self.drive_enabled = drive_enabled
        self.output_digital = state
        self.output_physical = GpioState.OFF
        self._driver = GpioOutput(drive_pin, active_high, state)
        self._sampler = GpioInput(sample_pin, active_high, pull_up)
        self.sample()

    def press_button(self, duration):
        duration_timer(self)
        self.state = GpioState.ON
        self.output_digital = GpioState.ON
        self.last_edge = EdgeType.RISING_EDGE
        if self.drive_enabled:
            self._driver.write(GpioState.ON)
        self.sample()

        def release():
            import time

            time.sleep(duration)
            duration_timer(self)
            self.state = GpioState.OFF
            self.output_digital = GpioState.OFF
            self.last_edge = EdgeType.FALLING_EDGE
            if self.drive_enabled:
                self._driver.write(GpioState.OFF)
            self.sample()

        import threading

        threading.Thread(target=release, daemon=True).start()

    def sample(self):
        self.output_physical = self._sampler.read()
        return self.output_physical

    def close(self):
        self._driver.write(GpioState.OFF)
        self._driver.close()
        self._sampler.close()


class PhysicalLed(Led):
    def __init__(self, name, state, sample_pin, active_high=True, pull_up=False):
        super().__init__(name, state)
        self.sample_pin = sample_pin
        self.output_digital = state
        self.output_physical = GpioState.OFF
        self._sampler = GpioInput(sample_pin, active_high, pull_up)
        self.sample()

    def start_blink(self, duration):
        self.output_digital = GpioState.ON if duration == 0 else GpioState.BLINK
        self.state = self.output_digital
        self.sample()

    def stop_blink(self):
        self.output_digital = GpioState.OFF
        self.state = GpioState.OFF
        self.output = GpioState.OFF
        self.sample()

    def sample(self):
        self.output_physical = self._sampler.read()
        self.output = self.output_physical
        return self.output_physical

    def close(self):
        self._sampler.close()


class PhysicalStopWatch(StopWatch):
    STATE_BITS = {
        0b00: TimerState.TIMER_IDLE,
        0b01: TimerState.TIMER_RUN,
        0b10: TimerState.TIMER_PAUSE,
        0b11: TimerState.TIMER_ALARM,
    }

    def __init__(
        self,
        pb_1,
        pb_2,
        pb_3,
        led_1,
        led_2,
        config,
        state_bit0_pin=None,
        state_bit1_pin=None,
        active_high=True,
    ):
        super().__init__(pb_1, pb_2, pb_3, led_1, led_2, config)
        self.state = None
        self.state_bit0 = GpioInput(state_bit0_pin, active_high)
        self.state_bit1 = GpioInput(state_bit1_pin, active_high)
        self.output_physical_state = None
        self.sample_state()

    def main(self):
        return

    def sample_state(self):
        bit0 = self.state_bit0.read() == GpioState.ON
        bit1 = self.state_bit1.read() == GpioState.ON
        self.output_physical_state = self.STATE_BITS[(bit1 << 1) | bit0]
        return self.output_physical_state

    def sync_expected(self, digital_twin):
        pairs = (
            (self.pb_1, digital_twin.pb_1, "state"),
            (self.pb_2, digital_twin.pb_2, "state"),
            (self.pb_3, digital_twin.pb_3, "state"),
            (self.led_1, digital_twin.led_1, "output"),
            (self.led_2, digital_twin.led_2, "output"),
        )
        for physical_component, digital_component, attr in pairs:
            physical_component.output_digital = getattr(digital_component, attr)
            if hasattr(physical_component, "sample"):
                physical_component.sample()
        self.sample_state()


def build_physical_twin(config, drive_pushbuttons=True):
    gpio = config.gpio
    return PhysicalStopWatch(
        PhysicalPushButton(
            "Physical Push Button 1",
            GpioState.OFF,
            gpio.pb1_drive_pin,
            gpio.pb1_sample_pin,
            gpio.active_high,
            drive_enabled=drive_pushbuttons,
        ),
        PhysicalPushButton(
            "Physical Push Button 2",
            GpioState.OFF,
            gpio.pb2_drive_pin,
            gpio.pb2_sample_pin,
            gpio.active_high,
            drive_enabled=drive_pushbuttons,
        ),
        PhysicalPushButton(
            "Physical Push Button 3",
            GpioState.OFF,
            gpio.pb3_drive_pin,
            gpio.pb3_sample_pin,
            gpio.active_high,
            drive_enabled=drive_pushbuttons,
        ),
        PhysicalLed("Physical Led 1", GpioState.OFF, gpio.led1_sample_pin, gpio.active_high),
        PhysicalLed("Physical Led 2", GpioState.OFF, gpio.led2_sample_pin, gpio.active_high),
        config,
        gpio.state_bit0_pin,
        gpio.state_bit1_pin,
        gpio.active_high,
    )


def sync_button_from_physical(digital_button, physical_button):
    physical_state = physical_button.sample()
    if physical_state == digital_button.state:
        return

    duration_timer(digital_button)
    digital_button.state = physical_state
    digital_button.last_edge = (
        EdgeType.RISING_EDGE
        if physical_state == GpioState.ON
        else EdgeType.FALLING_EDGE
    )
