#include "timer.h"

#include <xc.h>
#include "system.h"

#include <stdint.h>
#include <errno.h>


static volatile uint32_t ms_global;

static void timer_500khz_init(void);

static void timer_8mhz_init(void);


int timer_init(core_clock_freq_e core_clock) {
    if (core_clock != FREQ_8_MHZ && core_clock != FREQ_500_KHZ) return -EINVAL;

    timer_8mhz_init();
    timer_500khz_init();

    return timer_switch(core_clock);
}

int timer_switch(core_clock_freq_e core_clock) {
    int ret = 0;

    switch (core_clock) {
        case FREQ_8_MHZ:
            T2CONbits.TON = 0;
            T3CONbits.TON = 1;
            break;

        case FREQ_500_KHZ:
            T2CONbits.TON = 1;
            T3CONbits.TON = 0;
            break;

        default:
            ret = -EINVAL;
            break;
    }

    return ret;
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
    PR2 = 31249;
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
    PR3 = 62499;
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
