#include "delay.h"

#include "timer.h"

#include <stdint.h>
#include <stdbool.h>


void delay_init(delay_s *dev, uint32_t elapse_time_ms) {
    dev->start_ms = 0;
    dev->elapse_time_ms = elapse_time_ms;
    dev->enabled = false;
}

void delay_enable(delay_s *dev, uint32_t start_ms) {
    dev->start_ms = start_ms;
    dev->enabled = true;
}

void delay_disable(delay_s *dev) {
    dev->enabled = false;
}

bool delay_elapsed(delay_s *dev) {
    if (!dev->enabled) return false;

    uint32_t ms = millis();
    uint32_t elapsed = ms - dev->start_ms;

    if (elapsed >= dev->elapse_time_ms) {
        uint32_t overshoot = elapsed - dev->elapse_time_ms;
        dev->start_ms = ms - overshoot;
        return true;
    }

    return false;
}
