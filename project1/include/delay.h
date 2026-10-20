#ifndef _DELAY_H
#define _DELAY_H

#include <stdint.h>
#include <stdbool.h>


typedef struct delay_s {
    uint32_t start_ms;
    uint32_t elapse_time_ms;
    bool enabled;
} delay_s;


void delay_init(delay_s *dev, uint32_t elapse_time_ms);

void delay_enable(delay_s *dev, uint32_t start_ms);

void delay_disable(delay_s *dev);

bool delay_elapsed(delay_s *dev);

#endif
