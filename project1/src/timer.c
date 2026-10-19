#include "timer.h"
#include "superloop.h"
#include <xc.h>

#include "system.h"

#define TIMER_SCALE_NUMERATOR 60000UL
#define TIMER_SCALE_DENOMINATOR 66160UL

static volatile uint16_t ms_ticks;
static uint16_t timer2_initialized;
static uint16_t timer3_initialized;


#define MILLISECOND_SCALER 10
static void timer3_set_period(uint16_t clock_khz)
{
    if (clock_khz >= 32000u)
    {
        T3CONbits.TCKPS = 0x01;
        PR3 = 2008 * MILLISECOND_SCALER;
    }
    else if (clock_khz >= 8000u)
    {
        T3CONbits.TCKPS = 0x01;
        PR3 = 502* MILLISECOND_SCALER; //1 ms at 8 MHz
    }
    else if (clock_khz >= 4000u)
    {
        T3CONbits.TCKPS = 0x00;
        PR3 = 2008*MILLISECOND_SCALER; // This is not the correct number according to data sheet, got it via testing dont change
    }
    else
    {
        T3CONbits.TCKPS = 0x00;
        PR3 = 251 * MILLISECOND_SCALER;
    }
}

uint16_t timer_millis(void)
{
    return ms_ticks;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    IFS0bits.T2IF = 0;
    ms_ticks += 250u;
}

void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    _LATB8 = !_LATB8;
    ms_ticks += MILLISECOND_SCALER*1;
    IFS0bits.T3IF = 0;
}

void timer2_init(void)
{
    float clock_skew = 1;
    switch (DEFAULT_CLOCK_HZ)
    {
    case CLOCK_32MHZ:
        clock_skew = 0.99427129;
    break;
    case CLOCK_32KHZ:
        clock_skew = 1;
        break;
    case CLOCK_500KHZ:
        clock_skew = 0.963182;
        break;
    default:
        clock_skew = 1;
        break;
    }
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0x00;
    PR2 = (uint16_t)(3643 * clock_skew); // Retuned from a measured 79.873765 s runtime to 80 s
    _T2IP = 4;
    _T2IF = 0;
    _T2IE = 1;
    timer2_initialized = 1u;

    if (core_clock_khz == 32u)
    {
        T2CONbits.TON = 1;
    }
}

void timer2_reset_phase(void)
{
    uint16_t was_running = T2CONbits.TON;

    T2CONbits.TON = 0;
    TMR2 = 0;
    IFS0bits.T2IF = 0;
    T2CONbits.TON = was_running;
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
