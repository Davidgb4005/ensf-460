#ifndef PIC24_GPIO_H
#define PIC24_GPIO_H

#include "xc.h"

#define PB1 ((PORTB & 0x80) >> 7)      // Read RB7 and place its value in control bit 0
#define PB2 ((PORTB & 0x10) >> 3)      // Read RB4 and place its value in control bit 1
#define PB3 ((PORTA & 0x10) >> 2)      // Read RA4 and place its value in control bit 2
#define LED1_MASK 0x0200   // RB9
#define LED2_MASK 0x0040   // RA6
#define LED_TOGGLE_1 (LATB ^= LED1_MASK)
#define LED_ON_1     (LATB |= LED1_MASK)
#define LED_OFF_1    (LATB &= ~LED1_MASK)
#define LED_TOGGLE_2 (LATA ^= LED2_MASK)
#define LED_ON_2     (LATA |= LED2_MASK)
#define LED_OFF_2    (LATA &= ~LED2_MASK)


void IOinit();
void IOcheck();

#endif