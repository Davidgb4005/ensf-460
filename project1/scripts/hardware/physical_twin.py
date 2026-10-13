from pathlib import Path

from hardware.config import EdgeType, GpioState, TimerState
from hardware.digital_twin import Led, PushButton, StopWatch, duration_timer

try:
    import lgpio
except ImportError:
    lgpio = None


_GPIO_BACKEND = None


def _gpio_backend_error(exc, attempts=None):
    detail = ""
    if attempts:
        detail = " Tried: " + "; ".join(
            f"gpiochip{chip}: {error}" for chip, error in attempts
        )
    return RuntimeError(
        "GPIO backend is unavailable. If this is a Raspberry Pi, try running with "
        "`sudo`, install `python3-lgpio`, fix permissions for /dev/gpiochip*, "
        "or set `gpio_chip` in testing_config.py to the chip that contains the "
        "40-pin header GPIO lines. If this is not a Raspberry Pi, set GPIO pins "
        "to None or run without physical GPIO."
        f"{detail}"
    )


def configure_gpio_backend(gpio_chip):
    global _GPIO_BACKEND
    if lgpio is None:
        return

    chips = _candidate_gpio_chips(gpio_chip)
    last_error = None
    attempts = []
    for chip in chips:
        try:
            _GPIO_BACKEND = LgpioBackend(chip)
            return
        except Exception as exc:
            last_error = exc
            attempts.append((chip, exc))

    raise _gpio_backend_error(last_error, attempts)


def _candidate_gpio_chips(configured_chip):
    chips = []
    if configured_chip is not None:
        chips.append(configured_chip)
    else:
        chips.append(0)

    for path in sorted(Path("/dev").glob("gpiochip*")):
        suffix = path.name.removeprefix("gpiochip")
        if suffix.isdigit():
            chips.append(int(suffix))

    return list(dict.fromkeys(chips))


def _gpio_state(value):
    return GpioState.ON if value else GpioState.OFF


class LgpioBackend:
    def __init__(self, chip):
        self.chip = chip
        self.handle = lgpio.gpiochip_open(chip)

    def claim_input(self, pin, pull_up=False):
        flags = self._pull_flags(pull_up)
        if flags:
            lgpio.gpio_claim_input(self.handle, pin, flags)
        else:
            lgpio.gpio_claim_input(self.handle, pin)

    def claim_output(self, pin, initial_level):
        lgpio.gpio_claim_output(self.handle, pin, initial_level)

    def read(self, pin):
        return lgpio.gpio_read(self.handle, pin)

    def write(self, pin, level):
        lgpio.gpio_write(self.handle, pin, level)

    def free(self, pin):
        try:
            lgpio.gpio_free(self.handle, pin)
        except Exception:
            pass

    def _pull_flags(self, pull_up):
        if pull_up and hasattr(lgpio, "SET_PULL_UP"):
            return lgpio.SET_PULL_UP
        if not pull_up and hasattr(lgpio, "SET_PULL_DOWN"):
            return lgpio.SET_PULL_DOWN
        return 0


def _backend():
    if _GPIO_BACKEND is None:
        raise RuntimeError("GPIO backend was not configured before pin creation.")
    return _GPIO_BACKEND


class GpioInput:
    def __init__(self, pin, active_high=True, pull_up=False):
        self.pin = pin
        self.active_high = active_high
        self._claimed = False
        if pin is not None and lgpio is not None:
            _backend().claim_input(pin, pull_up)
            self._claimed = True

    def read(self):
        if not self._claimed:
            return GpioState.OFF
        value = _backend().read(self.pin)
        if not self.active_high:
            value = not value
        return _gpio_state(value)

    def close(self):
        if self._claimed:
            _backend().free(self.pin)
            self._claimed = False


class GpioOutput:
    def __init__(self, pin, active_high=True, initial_state=GpioState.OFF):
        self.pin = pin
        self.active_high = active_high
        self._claimed = False
        if pin is not None and lgpio is not None:
            _backend().claim_output(pin, self._level(initial_state))
            self._claimed = True

    def write(self, state):
        if self._claimed:
            _backend().write(self.pin, self._level(state))

    def close(self):
        if self._claimed:
            _backend().free(self.pin)
            self._claimed = False

    def _level(self, state):
        active = state == GpioState.ON
        if not self.active_high:
            active = not active
        return int(active)


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
    configure_gpio_backend(gpio.gpio_chip)
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
