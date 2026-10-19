/**
 * Manages a timer for the running program using a common global time counter.
 * When the system core clock frequency changes, timer_switch() should be called
 * to switch the timer that is used. This should be done by:
 *
 * timer_disable() -> set_core_clock() -> timer_switch().
 */

#ifndef _TIMER_H
#define _TIMER_H

#include "system.h"

#include <stdint.h>

/**
 * Intialize both timers for and enable the one for the selected clock
 * frequency.
 *
 * Returns -EINVAL for invalid clock frequency, or 0 on success.
 */
int timer_init(core_clock_freq_e core_clock);

/**
 * Switch the timer being used to the one for the selected clock frequency.
 *
 * Enables the timer if it is disabled.
 *
 * Returns -EINVAL for invalid clock frequency, or 0 on success.
 */
int timer_switch(core_clock_freq_e core_clock);

/**
 * Disables both timer sources.
 */
void timer_disable(void);

/**
 * Resets both timer values, as well as the global time counter.
 */
void timer_reset(void);

/**
 * Returns the current global time counter, in milliseconds.
 */
uint32_t millis(void);

/**
 * Returns the time elapsed from a start time (received by millis()), in milliseconds.
 */
uint32_t elapsed_ms(uint32_t start);

#endif
