#ifndef PIC24_TIMER_H
#define PIC24_TIMER_H

#include <stdint.h>

typedef struct{
    uint32_t PT;
    uint16_t IN;
    uint32_t ET;
    uint16_t Q;
    uint16_t reset;
}timer_delay;


void timer2Init(void);
uint16_t TON_timer(timer_delay * delay_struct);
uint16_t TON_timer_micros(timer_delay * delay_struct);
uint16_t TOF_timer(timer_delay * delay_struct);
uint32_t millis(void);
uint32_t micros(void);


#endif
