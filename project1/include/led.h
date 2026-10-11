#ifndef _LED_H
#define _LED_H


#include "stdint.h"

typedef enum{
    BLINK,
    PWM,
    DIGITAL,
}led_mode_e;

typedef struct
{
    uint16_t led_pin;
    union
    {
        uint16_t state;
        struct 
        {
            uint16_t output : 1;
            uint16_t enabled : 1;
            uint16_t paused : 1;
            uint16_t reserved : 13;
        }st_bits;
        
        /* data */
    };
    uint16_t freq;
    uint16_t prev_time;
    uint16_t mode;
    uint16_t blink_delay;
}led_t;



uint16_t updateLed(led_t * led);
uint16_t writeLed(led_t * led,uint16_t input);
uint16_t toggleLed(led_t * led);
#endif
