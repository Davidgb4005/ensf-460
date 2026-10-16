#ifndef SUPERLOOP_H
#define SUPERLOOP_H

#include <stdint.h>
#include "error.h"
#include "timer.h"

extern volatile uint16_t elapsed_value;
extern volatile timer_state_e state;

static int16_t clamp_preset_value(int16_t value);
error_e superloop_setup(void);

error_e superloop_loop(void);

#endif
