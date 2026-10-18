#ifndef _TIMER_H
#define _TIMER_H

#include "system.h"

#include <stdint.h>

typedef struct delay_s {
    uint32_t start;
} delay_s;


int timer_init(core_clock_freq_e core_clock_freq_e);

int timer_switch(core_clock_freq_e core_clock);

void timer_disable(void);

uint32_t millis(void);

uint32_t elapsed_ms(uint32_t start);

#endif
