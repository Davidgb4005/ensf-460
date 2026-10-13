import unittest

from testing.twin import DEFAULT_CONFIG, GpioState, TimerState, build_digital_model
from testing.twin.protocol import LED_STATES, TIMER_STATES


class TimerModelTests(unittest.TestCase):
    def test_model_is_built_from_testing_configuration(self):
        model = build_digital_model(DEFAULT_CONFIG)

        self.assertIs(model.config, DEFAULT_CONFIG)
        self.assertEqual(model.state, TimerState.TIMER_IDLE)
        self.assertEqual(model.preset_timer, 0)
        self.assertEqual(model.elapsed_timer, 0)
        self.assertEqual(model.pb_1.state, GpioState.OFF)
        self.assertEqual(model.led_1.state, GpioState.OFF)
        self.assertIsNone(DEFAULT_CONFIG.uart.port)
        self.assertEqual(DEFAULT_CONFIG.uart.baud_rate, 9600)

    def test_uart_codes_match_model_enums(self):
        for state in TimerState:
            name = state.name.removeprefix("TIMER_")
            self.assertEqual(TIMER_STATES[state.value], name)
        for state in GpioState:
            self.assertEqual(LED_STATES[state.value], state.name)


if __name__ == "__main__":
    unittest.main()
