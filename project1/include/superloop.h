#ifndef SUPERLOOP_H
#define SUPERLOOP_H

#include <stdint.h>
#include "timer.h"

#define BAUD32MHZ 500000UL
#define BAUD8MHZ 500000UL
#define BAUD4MHZ 500000UL
#define BAUD1MHZ 125000UL
#define BAUD500KHZ 62500UL
#define BAUD32KHZ 300UL

#define CLOCK_32MHZ 32000000UL
#define CLOCK_8MHZ 8000000UL
#define CLOCK_4MHZ 4000000UL
#define CLOCK_1MHZ 1000000UL
#define CLOCK_500KHZ 500000UL
#define CLOCK_32KHZ 32000UL
#ifndef DEFAULT_CLOCK_HZ
#define DEFAULT_CLOCK_HZ CLOCK_500KHZ
#endif

extern volatile uint16_t elapsed_value;
extern volatile timer_state_e state;

void superloop_setup(void);
void superloop_loop(void);

#endif
