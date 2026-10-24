#include "system.h"

#include <xc.h>

#include <stdint.h>


uint16_t core_clock_khz = 8000;

void set_core_clock(core_clock_freq_e freq) {
    // Set clock speed
    SRbits.IPL = 7;
    set_core_clock_raw(freq); 
    SRbits.IPL = 0;

    update_core_clock_khz(freq);
}

void set_core_clock_raw(core_clock_freq_e freq) {
    CLKDIVbits.RCDIV = 0;
    OSCTUNbits.TUN = -5;
    __builtin_write_OSCCONH(freq);
    __builtin_write_OSCCONL(OSCCONL | 0x01);
    while (OSCCONbits.OSWEN == 1) (void)0;
}

void update_core_clock_khz(core_clock_freq_e freq){ //Update Core to new freq
    switch (freq) {
        case FREQ_8_MHZ: // if 8Mhz is selected set core to 8 Mhz
            core_clock_khz = 8000;
            break;

        case FREQ_500_KHZ:// If 500 khjz is seleceted set core to 500 khz
            core_clock_khz = 500;
            break;

        case FREQ_32_KHZ: // if 32 khz seleected set core to 32 khz
            core_clock_khz = 32;
            break;
    }
}
