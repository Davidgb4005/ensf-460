#ifndef _SUPERLOOP_H
#define _SUPERLOOP_H

#include <stdint.h>
#include "error.h"
#include "timer.h"

extern const char *messages[];
extern volatile uint32_t elapsed_value;
extern volatile timer_state_e state;

error_e superloop_setup(void);

error_e superloop_loop(void);

#endif
