#ifndef _LED_H
#define _LED_H


#include "stdint.h"
#include "error.h"

typedef enum{
    LED_BLINK,
    LED_PWM,
    LED_DIGITAL,
} led_mode_e;

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
    led_mode_e mode;
    uint16_t blink_delay;
} led_s;



error_e led_update(led_s *led);
error_e led_write(led_s *led, uint16_t input);
error_e led_toggle(led_s *led);
#endif
