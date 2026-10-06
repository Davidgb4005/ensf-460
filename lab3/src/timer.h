#ifndef PIC24_TIMER_H
#define PIC24_TIMER_H

#include <stdint.h>



void timer2_init(void);
void delay_ms(uint16_t delay);
uint16_t debounce_delay_ms(uint16_t condition,uint16_t ms);

#endif
