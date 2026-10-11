#include "timer.h"
#include "led.h"
#include "gpio.h"

uint16_t updateLed(led_t *led)
{
    if (!led->st_bits.enabled) {
        digitalWrite(led->led_pin, 0);
        return 1;
    }

    if (led->st_bits.paused) {
        return 1;
    }

    switch (led->mode)
    {
    case BLINK:
        if ((uint16_t)(millis() - led->prev_time) >= led->blink_delay) {
            led->st_bits.output ^= 1;
            led->prev_time += led->blink_delay;
        }
        break;

    case DIGITAL:
        break;

    case PWM:
        return 1;

    default:
        return 1;
    }

    digitalWrite(led->led_pin, led->st_bits.output);
    return 0;
}

uint16_t writeLed(led_t *led, uint16_t input)
{
    if (!led->st_bits.enabled) {
        digitalWrite(led->led_pin, 0);
        return 1;
    }

    if (led->st_bits.paused) {
        return 1;
    }

    switch (led->mode)
    {
    case BLINK:
        return 1;

    case DIGITAL:
        led->st_bits.output = !!input;
        break;

    case PWM:
        return 1;

    default:
        return 1;
    }

    digitalWrite(led->led_pin, led->st_bits.output);
    return 0;
}

uint16_t toggleLed(led_t *led)
{
    if (!led->st_bits.enabled) {
        digitalWrite(led->led_pin, 0);
        return 1;
    }

    if (led->st_bits.paused) {
        return 1;
    }

    switch (led->mode)
    {
    case BLINK:
        return 1;

    case DIGITAL:
        led->st_bits.output ^= 1;
        break;

    case PWM:
        return 1;

    default:
        return 1;
    }

    digitalWrite(led->led_pin, led->st_bits.output);
    return 0;
}