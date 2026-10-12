#include "timer.h"
#include "led.h"
#include "gpio.h"

error_e led_update(led_s *led)
{
    if (!led->st_bits.enabled) {
        gpio_digital_write(led->led_pin, 0);
        led->prev_time = timer_millis();
        return ERROR_UNSUPPORTED_MODE;
    }

    if (led->st_bits.paused) {
        led->prev_time = timer_millis();
        return ERROR_UNSUPPORTED_MODE;
    }

    switch (led->mode)
    {
    case LED_BLINK:
        if ((uint16_t)(timer_millis() - led->prev_time) >= led->blink_delay) {
            led->st_bits.output ^= 1;
            led->prev_time += led->blink_delay;
        }
        break;

    case LED_DIGITAL:
        break;

    case LED_PWM:
        return ERROR_UNSUPPORTED_MODE;

    default:
        return ERROR_UNSUPPORTED_MODE;
    }

    gpio_digital_write(led->led_pin, led->st_bits.output);
    return ERROR_NONE;
}

error_e led_write(led_s *led, uint16_t input)
{
    if (!led->st_bits.enabled) {
        gpio_digital_write(led->led_pin, 0);
        return ERROR_UNSUPPORTED_MODE;
    }

    if (led->st_bits.paused) {
        return ERROR_UNSUPPORTED_MODE;
    }

    switch (led->mode)
    {
    case LED_BLINK:
        return ERROR_UNSUPPORTED_MODE;

    case LED_DIGITAL:
        led->st_bits.output = !!input;
        break;

    case LED_PWM:
        return ERROR_UNSUPPORTED_MODE;

    default:
        return ERROR_UNSUPPORTED_MODE;
    }

    gpio_digital_write(led->led_pin, led->st_bits.output);
    return ERROR_NONE;
}

error_e led_toggle(led_s *led)
{
    if (!led->st_bits.enabled) {
        gpio_digital_write(led->led_pin, 0);
        return ERROR_UNSUPPORTED_MODE;
    }

    if (led->st_bits.paused) {
        return ERROR_UNSUPPORTED_MODE;
    }

    switch (led->mode)
    {
    case LED_BLINK:
        return ERROR_UNSUPPORTED_MODE;

    case LED_DIGITAL:
        led->st_bits.output ^= 1;
        break;

    case LED_PWM:
        return ERROR_UNSUPPORTED_MODE;

    default:
        return ERROR_UNSUPPORTED_MODE;
    }

    gpio_digital_write(led->led_pin, led->st_bits.output);
    return ERROR_NONE;
}
