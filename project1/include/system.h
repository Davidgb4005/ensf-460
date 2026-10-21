/**
 * Defines utilities for core system configuration, such as core clock frequency
 */

#ifndef _SYSTEM_H
#define _SYSTEM_H

#include <stdint.h>


/**
 * Possible core clock frequencies that can be set.
 *
 * The backing integers are the values that should be placed in the OSCCON NOSC field.
 */
typedef enum core_clock_freq {
    FREQ_8_MHZ   = 0b111,
    FREQ_500_KHZ = 0b110,
    FREQ_32_KHZ  = 0b101,
} core_clock_freq_e;

/**
 * Stores the core clock input frequency. The effective running frequency will be
 * core_clock_khz / 2.
 */
extern uint16_t core_clock_khz;

/**
 * Changes the core clock input frequency.
 * Updates core_clock_khz.
 */
void set_core_clock(core_clock_freq_e freq);

/**
 * Sets the OSCCON NOSC bits, and waits for the clock to successfully switch.
 */
void set_core_clock_raw(core_clock_freq_e freq);

/**
 * Updates core_clock_khz based on the given frequency.
 */
void update_core_clock_khz(core_clock_freq_e freq);

#endif
