#ifndef PIC24_TIMER_H
#define PIC24_TIMER_H

#include <stdint.h>

typedef enum
{
    TIMER_IDLE,
    TIMER_RUN,
    TIMER_PAUSE,
    TIMER_ALARM,
} timer_state_e;

uint16_t timer2_init(void);
uint16_t timer_millis(void);

#endif
