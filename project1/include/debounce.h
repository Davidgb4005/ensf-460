#ifndef _DEBOUNCE_H
#define _DEBOUNCE_H
#include "stdint.h"
#include "gpio.h"
typedef enum
{
    DEBOUNCE_NO_EDGE,
    DEBOUNCE_FALLING_EDGE,
    DEBOUNCE_RISING_EDGE,

} debounce_edge_e;
typedef struct
{
    uint16_t pin;
    union
    {
        uint8_t state;
        struct
        {
            uint8_t pb_current_state : 1;
            uint8_t most_recent_edge : 2;
            uint8_t db_started : 1;
            uint8_t db_start_state : 1;
            uint8_t db_finished : 1;
            uint8_t reserved : 1;
        } state_bits;
    };
    uint16_t debounce_delay;
    uint16_t debounce_start_time;
    uint16_t falling_edge_time;
    uint16_t rising_edge_time;
} debounce_s;

uint8_t debounce_read(debounce_s *pb);
uint16_t debounce_previous_duration(debounce_s *pb);
uint16_t debounce_state_duration(debounce_s *pb);
uint16_t debounce_edge_read(debounce_s *pb, debounce_edge_e edge_read);



#endif
