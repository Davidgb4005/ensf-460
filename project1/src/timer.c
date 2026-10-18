#include "timer.h"

#include <xc.h>

#include "system.h"

static volatile uint16_t ms_ticks;
static uint16_t timer2_initialized;
static uint16_t timer3_initialized;

static void timer3_set_period(uint16_t clock_khz)
{
    if (clock_khz >= 8000u)
    {
        T3CONbits.TCKPS = 0x01;
        PR3 = (uint16_t)((clock_khz / 16u) - 1u);
    }
    else if (clock_khz >= 4000u)
    {
        T3CONbits.TCKPS = 0x00;
        PR3 = 1999;
    }
    else
    {
        T3CONbits.TCKPS = 0x00;
        PR3 = 249;
    }
}

uint16_t timer_millis(void)
{
    return ms_ticks;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    ms_ticks += 250u;
    IFS0bits.T2IF = 0;
}

void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    ms_ticks++;
    IFS0bits.T3IF = 0;
}

void timer2_init(void)
{
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0x00;
    PR2 = 3999;
    _T2IP = 4;
    _T2IF = 0;
    _T2IE = 1;
    timer2_initialized = 1u;

    if (core_clock_khz == 32u)
    {
        T2CONbits.TON = 1;
    }
}

void timer3_init(void)
{
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0;
    T3CONbits.TGATE = 0;
    TMR3 = 0;
    timer3_set_period(core_clock_khz);
    _T3IP = 4;
    _T3IF = 0;
    _T3IE = 1;
    timer3_initialized = 1u;

    if (core_clock_khz != 32u)
    {
        T3CONbits.TON = 1;
    }
}

void timers_prepare_clock_change(void)
{
    if (timer2_initialized != 0u)
    {
        T2CONbits.TON = 0;
    }

    if (timer3_initialized != 0u)
    {
        T3CONbits.TON = 0;
        TMR3 = 0;
        IFS0bits.T3IF = 0;
    }
}

void timers_update_clock(uint16_t clock_khz)
{
    if (clock_khz == 32u)
    {
        if (timer3_initialized != 0u)
        {
            T3CONbits.TON = 0;
        }

        if (timer2_initialized != 0u)
        {
            IFS0bits.T2IF = 0;
            T2CONbits.TON = 1;
        }
        return;
    }

    if (timer2_initialized != 0u)
    {
        T2CONbits.TON = 0;
    }

    if (timer3_initialized != 0u)
    {
        timer3_set_period(clock_khz);
        IFS0bits.T3IF = 0;
        T3CONbits.TON = 1;
    }
}
