#include "system.h"

#include <xc.h>

#include "timer.h"
#include "uart.h"

uint16_t core_clock_khz = 8000u;

void set_core_clock(core_clock_freq_e freq)
{
    uint16_t next_clock_khz;
    uint16_t previous_ipl;
    uint8_t oscillator_source;
    uint8_t frc_divider;
    uint8_t wait_for_pll = 0u;

    switch (freq)
    {
    case FREQ_32_MHZ:
        next_clock_khz = 32000u;
        oscillator_source = 0b001;
        frc_divider = 0b000;
        wait_for_pll = 1u;
        break;
    case FREQ_8_MHZ:
        next_clock_khz = 8000u;
        oscillator_source = 0b111;
        frc_divider = 0b000;
        break;
    case FREQ_4_MHZ:
        next_clock_khz = 4000u;
        oscillator_source = 0b111;
        frc_divider = 0b001;
        break;
    case FREQ_500_KHZ:
        next_clock_khz = 500u;
        oscillator_source = 0b110;
        frc_divider = 0b000;
        break;
    case FREQ_32_KHZ:
        next_clock_khz = 32u;
        oscillator_source = 0b101;
        frc_divider = 0b000;
        break;
    default:
        return;
    }

    if (next_clock_khz == core_clock_khz)
    {
        return;
    }

    uart_wait_for_tx();

    previous_ipl = SRbits.IPL;
    SRbits.IPL = 7;
    timers_prepare_clock_change();
    CLKDIVbits.RCDIV = frc_divider;
    __builtin_write_OSCCONH(oscillator_source);
    __builtin_write_OSCCONL(OSCCON | 0x01u);
    while (OSCCONbits.OSWEN != 0u)
    {
    }
    if (wait_for_pll != 0u)
    {
        while (OSCCONbits.LOCK == 0u)
        {
        }
    }

    core_clock_khz = next_clock_khz;
    timers_update_clock(core_clock_khz);
    uart_update_clock(core_clock_khz);
    SRbits.IPL = previous_ipl;
}
