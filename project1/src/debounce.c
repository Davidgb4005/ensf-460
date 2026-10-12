#include <stdio.h>
#include "debounce.h"
#include "timer.h"
#include "uart.h"

uint8_t debounce_read(debounce_s *pb)
{
    pb->state_bits.db_finished = 0;
    uint16_t input = gpio_digital_read(pb->pin);
    if (!pb->state_bits.db_started || input != pb->state_bits.db_start_state)
    {
        pb->debounce_start_time = timer_millis();
        pb->state_bits.db_started = 1;
        pb->state_bits.db_start_state = input;
    }

    if ((uint16_t)(timer_millis() - pb->debounce_start_time) >= pb->debounce_delay && input != pb->state_bits.pb_current_state)
    {
        pb->state_bits.pb_current_state = input;
        pb->state_bits.db_finished = 1;

        if (pb->state_bits.pb_current_state)
        {
            pb->rising_edge_time = timer_millis();
            pb->state_bits.most_recent_edge = DEBOUNCE_RISING_EDGE;
        }
        else
        {
            pb->state_bits.most_recent_edge = DEBOUNCE_FALLING_EDGE;
            pb->falling_edge_time = timer_millis();
        }
    }
    return pb->state_bits.pb_current_state;
}

uint16_t debounce_previous_duration(debounce_s *pb)
{
    if (pb->rising_edge_time > pb->falling_edge_time)
    {
        return pb->rising_edge_time - pb->falling_edge_time;
    }
    else
    {
        return pb->falling_edge_time - pb->rising_edge_time;
    }
}
uint16_t debounce_state_duration(debounce_s *pb)
{
    if (pb->state_bits.pb_current_state)
    {
        return pb->rising_edge_time - timer_millis();
    }
    else
    {
        return pb->falling_edge_time - timer_millis();
    }
}
uint16_t debounce_edge_read(debounce_s *pb, debounce_edge_e edge_read)
{
    if (pb->state_bits.most_recent_edge == edge_read)
    {
        pb->state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
        return 1;
    }
    return 0;
}
