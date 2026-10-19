#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

typedef enum
{
    FREQ_4_MHZ = 0b000,
    FREQ_1_MHZ = 0b010,
    FREQ_32_MHZ = 0b001,
    FREQ_8_MHZ = 0b111,
    FREQ_500_KHZ = 0b110,
    FREQ_32_KHZ = 0b101,
} core_clock_freq_e;

extern uint16_t core_clock_khz;

void set_core_clock(core_clock_freq_e freq);

#endif
