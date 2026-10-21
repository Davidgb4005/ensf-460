#include "timer.h"

#include <xc.h>
#include "system.h"

#include <stdint.h>
#include <errno.h>


#define T2_PERIOD 31250U
#define T3_PERIOD 62500U

#define COST_SWITCH_TO_8M_US   134
#define COST_SWITCH_TO_500K_US 1865

static volatile uint32_t ms_global;
static core_clock_freq_e cur_clock;

/**
 * Initializes timer 2 for a 500 kHz clock.
 */
static void timer_500khz_init(void);

/**
 * Initializes timer 3 for an 8 MHz clock.
 */
static void timer_8mhz_init(void);


int timer_init(core_clock_freq_e core_clock) {
    if (core_clock != FREQ_8_MHZ && core_clock != FREQ_500_KHZ) return -EINVAL;

    timer_8mhz_init();
    timer_500khz_init();

    switch (core_clock) {
        case FREQ_8_MHZ:
            T2CONbits.TON = 0;
            T3CONbits.TON = 1;
            return 0;            

        case FREQ_500_KHZ:
            T3CONbits.TON = 0;
            T2CONbits.TON = 1;
            return 0;

        default:
            return -EINVAL;
    }

    cur_clock = core_clock;
}

int timer_switch(core_clock_freq_e core_clock) {
    if (core_clock != FREQ_8_MHZ && core_clock != FREQ_500_KHZ) return -EINVAL;
    if (core_clock == cur_clock) return 0;

    // Disable interrupts
    SRbits.IPL = 7;

    set_core_clock_raw(core_clock);

    uint32_t t;
    switch (core_clock) {
        default:
        case FREQ_8_MHZ:
            T2CONbits.TON = 0;

            t = ((uint32_t)TMR2 * T3_PERIOD) / T2_PERIOD + (COST_SWITCH_TO_8M_US / 2);
            if (t >= T3_PERIOD) {
              t -= T3_PERIOD;
              ms_global += 125;
            }
            TMR3 = (uint16_t)t;

            T3CONbits.TON = 1;
            TMR2 = 0;
            break;

        case FREQ_500_KHZ:
            T3CONbits.TON = 0;

            t = ((uint32_t)TMR3 * T2_PERIOD) / T3_PERIOD + (COST_SWITCH_TO_500K_US / 4);
            if (t >= T2_PERIOD) {
              t -= T2_PERIOD;
              ms_global += 125;
            }
            TMR2 = (uint16_t)t;

            T2CONbits.TON = 1;
            TMR3 = 0;
            break;
    }

    // Re-enable interrupts
    SRbits.IPL = 0;

    update_core_clock_khz(core_clock);
    cur_clock = core_clock;
    return 0;
}

void timer_disable(void) {
    T2CONbits.TON = 0;
    T3CONbits.TON = 0;
}

void timer_reset(void) {
    TMR2 = 0;
    TMR3 = 0;
    ms_global = 0;
}

uint32_t millis(void) {
    uint16_t ie2 = _T2IE;
    uint16_t ie3 = _T3IE;

    _T2IE = 0;
    _T3IE = 0;

    uint32_t ms = ms_global;

    _T2IE = ie2;
    _T3IE = ie3;

    return ms;
}

uint32_t elapsed_ms(uint32_t start) {
    return millis() - start;
}

static void timer_500khz_init(void) {
    T2CONbits.TON = 0;

    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0x00;

    TMR2 = 0;
    PR2 = T2_PERIOD - 1;
    _T2IP = 4;
    _T2IF = 0;
    _T2IE = 1;
}

static void timer_8mhz_init(void) {
    T3CONbits.TON = 0;

    T3CONbits.TCS = 0;
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0x01;

    TMR3 = 0;
    PR3 = T3_PERIOD - 1;
    _T3IP = 4;
    _T3IF = 0;
    _T3IE = 1;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void) {
    IFS0bits.T2IF = 0;
    ms_global += 125;
}

void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void) {
    IFS0bits.T3IF = 0;
    ms_global += 125;
}
