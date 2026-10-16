/**
 * Defines utilities for core system configuration, such as core clock frequency
 */

#ifndef _SYSTEM_H
#define _SYSTEM_H

#include <stdint.h>


enum core_clock_freq {
    FREQ_8_MHZ   = 0b111,
    FREQ_500_KHZ = 0b110,
    FREQ_32_KHZ  = 0b100,
};

/**
 * Sets the core clock input frequency. The effective running frequency will be
 * core_clock_khz / 2.
 */
extern uint16_t core_clock_khz;

/**
 * Changes the core clock input frequency.
 *
 * Returns a negative errno on failure, or 0 on success.
 */
void set_core_clock(enum core_clock_freq freq);

#endif
