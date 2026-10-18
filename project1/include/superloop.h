#ifndef SUPERLOOP_H
#define SUPERLOOP_H

#include <stdint.h>
#include "timer.h"

extern volatile uint16_t elapsed_value;

void superloop_setup(void);
void superloop_loop(void);

#endif
