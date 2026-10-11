#ifndef _DEBOUNCE_H
#define _DEBOUNCE_H
#include "stdint.h"
#include "gpio.h"
typedef enum
{
    NO_EDGE,
    FALLING_EDGE,
    RISING_EDGE,

} edge_type;
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
} debounce;

uint8_t debounce_read(debounce *pb);
uint16_t debounce_previous_duration(debounce *pb);
uint16_t debounce_state_duration(debounce *pb);
uint16_t debounce_edge_read(debounce *pb, edge_type edge_read);


#endif
