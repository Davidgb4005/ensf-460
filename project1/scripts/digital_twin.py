import threading
import time

from testing_config import EdgeType, GpioState, TimerState


def duration_timer(component):
    component.state_duration = time.perf_counter() - component.timer_start
    component.timer_start = time.perf_counter()


class PushButton:
    def __init__(self, name, state):
        self.name = name
        self.state = state
        self.last_edge = EdgeType.CONSUMED_EDGE
        self.state_duration = 0
        self.timer_start = time.perf_counter()

    def press_button(self, duration):
        duration_timer(self)
        self.state = GpioState.ON
        self.last_edge = EdgeType.RISING_EDGE

        def release():
            time.sleep(duration)
            duration_timer(self)
            self.state = GpioState.OFF
            self.last_edge = EdgeType.FALLING_EDGE

        threading.Thread(target=release, daemon=True).start()


class Led:
    def __init__(self, name, state):
        self.name = name
        self.state = state
        self.output = GpioState.OFF
        self.stop_event = threading.Event()
        self.pause_event = threading.Event()
        self.blink_thread = None
        self.blink_duration = None

    def start_blink(self, duration):
        if duration == 0:
            self.stop_event.set()
            self.blink_duration = None
            self.state = GpioState.ON
            self.output = GpioState.ON
            return

        if (
            self.state == GpioState.BLINK
            and self.blink_duration == duration
            and self.blink_thread is not None
            and self.blink_thread.is_alive()
        ):
            return

        self.stop_event.set()
        self.stop_event = threading.Event()
        event = self.stop_event
        self.state = GpioState.BLINK
        self.blink_duration = duration

        def delay(duration_s):
            remaining = duration_s
            previous = time.monotonic()

            while remaining > 0:
                if event.is_set():
                    return False

                if self.pause_event.is_set():
                    previous = time.monotonic()
                    if event.wait(0.01):
                        return False
                    continue

                now = time.monotonic()
                remaining -= now - previous
                previous = now

                if event.wait(min(0.01, max(0, remaining))):
                    return False

            return True

        def blink_thread():
            while not event.is_set():
                self.output = GpioState.ON
                if not delay(duration):
                    break

                self.output = GpioState.OFF
                if not delay(duration):
                    break

        self.blink_thread = threading.Thread(target=blink_thread, daemon=True)
        self.blink_thread.start()

    def pause_blink(self):
        self.pause_event.set()

    def resume_blink(self):
        self.pause_event.clear()

    def stop_blink(self):
        self.stop_event.set()
        self.blink_duration = None
        self.state = GpioState.OFF
        self.output = GpioState.OFF


class StopWatch:
    def __init__(self, pb_1, pb_2, pb_3, led_1, led_2, config):
        self.state = TimerState.TIMER_IDLE
        self.pb_1 = pb_1
        self.pb_2 = pb_2
        self.pb_3 = pb_3
        self.led_1 = led_1
        self.led_2 = led_2
        self.preset_timer = 0
        self.elapsed_timer = 0
        self.config = config

    def main(self):
        def main_thread():
            counter = 0
            prev_buttons = 0
            request_state = TimerState.TIMER_IDLE
            prev_state = TimerState.TIMER_IDLE
            start_time = time.monotonic()

            while True:
                buttons = (
                    (self.pb_1.state == GpioState.ON) << 0
                    | (self.pb_2.state == GpioState.ON) << 1
                    | (self.pb_3.state == GpioState.ON) << 2
                )

                if buttons != prev_buttons or self.state != prev_state:
                    start_time = time.monotonic() + self.config.timing.debounce_delay_s
                    counter = 0

                if request_state != self.state:
                    self.state = request_state
                    start_time = time.monotonic()
                    counter = 0
                    double_press = 0
                    self.pb_3.last_edge = EdgeType.CONSUMED_EDGE
                    self.pb_2.last_edge = EdgeType.CONSUMED_EDGE
                    self.pb_1.last_edge = EdgeType.CONSUMED_EDGE

                self.preset_timer = max(
                    self.config.preset.min_preset_s,
                    min(self.config.preset.max_preset_s, self.preset_timer),
                )

                match self.state:
                    case TimerState.TIMER_IDLE:
                        if ((self.pb_1.last_edge == EdgeType.FALLING_EDGE or self.pb_2.last_edge == EdgeType.FALLING_EDGE) and double_press == 1):
                            request_state = TimerState.TIMER_RUN
                            self.elapsed_timer = self.preset_timer
                            double_press = 0
                        else:
                            match buttons:
                                case 0b001:
                                    next_counter = self._repeat_adjust(
                                        start_time,
                                        counter,
                                        self.config.preset.short_second_step_s,
                                        self.config.preset.long_second_step_s,
                                    )
                                    if next_counter is not None:
                                        counter = next_counter
                                        start_time = time.monotonic()
                                case 0b010:
                                    next_counter = self._repeat_adjust(
                                        start_time,
                                        counter,
                                        self.config.preset.short_minute_step_s,
                                        self.config.preset.long_minute_step_s,
                                    )
                                    if next_counter is not None:
                                        counter = next_counter
                                        start_time = time.monotonic()
                                case 0b101:
                                    next_counter = self._repeat_adjust(
                                        start_time,
                                        counter,
                                        -self.config.preset.short_second_step_s,
                                        -self.config.preset.long_second_step_s,
                                    )
                                    if next_counter is not None:
                                        counter = next_counter
                                        start_time = time.monotonic()
                                case 0b110:
                                    next_counter = self._repeat_adjust(
                                        start_time,
                                        counter,
                                        -self.config.preset.short_minute_step_s,
                                        -self.config.preset.long_minute_step_s,
                                    )
                                    if next_counter is not None:
                                        counter = next_counter
                                        start_time = time.monotonic()
                                case 0b011:
                                    double_press = 1
                                    if time.monotonic() - start_time >= self.config.timing.reset_delay_s:
                                        start_time = time.monotonic()
                                        self.preset_timer = 0

                    case TimerState.TIMER_RUN:
                        self.led_1.start_blink(self.config.timing.led1_blink_delay_s)
                        if self.elapsed_timer == 0:
                            request_state = TimerState.TIMER_ALARM
                        elif self.pb_3.last_edge == EdgeType.RISING_EDGE:
                            request_state = TimerState.TIMER_PAUSE
                            counter = 0
                        elif time.monotonic() - start_time >= self.config.timing.count_delay_s:
                            start_time = time.monotonic()
                            self.elapsed_timer -= 1

                    case TimerState.TIMER_PAUSE:
                        if self.pb_3.last_edge == EdgeType.RISING_EDGE:
                            request_state = TimerState.TIMER_RUN
                        elif time.monotonic() - start_time >= self.config.timing.clear_delay_s and (buttons & 0x4):
                            request_state = TimerState.TIMER_IDLE
                            self.elapsed_timer = 0

                    case TimerState.TIMER_ALARM:
                        self.led_1.start_blink(0)
                        self.led_2.start_blink(self.config.timing.led2_blink_delay_s)

                        if (buttons & 3) > 0:
                            request_state = TimerState.TIMER_IDLE
                            self.led_1.stop_blink()
                            self.led_2.stop_blink()

                prev_state = self.state
                prev_buttons = buttons

        threading.Thread(target=main_thread, daemon=True).start()

    def _repeat_adjust(self, start_time, counter, short_step, long_step):
        if time.monotonic() - start_time < self.config.timing.increment_delay_s:
            return None

        counter += 1
        if counter > self.config.timing.fast_increment_count:
            self.preset_timer += long_step
        else:
            self.preset_timer += short_step
        return counter


def build_digital_twin(config):
    pb_1 = PushButton("Push Button 1", GpioState.OFF)
    pb_2 = PushButton("Push Button 2", GpioState.OFF)
    pb_3 = PushButton("Push Button 3", GpioState.OFF)
    led_1 = Led("Led 1", GpioState.OFF)
    led_2 = Led("Led 2", GpioState.OFF)
    return StopWatch(pb_1, pb_2, pb_3, led_1, led_2, config)
