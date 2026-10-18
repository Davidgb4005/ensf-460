#include "timer.h"
#include <xc.h>
#include "superloop.h"

#define OSC_FRC 0b000
#define OSC_FRC_PLL 0b001
#define OSC_LPRC 0b101
#define OSC_LP_FRC 0b110
#define OSC_FRC_DIV 0b111

#define FRC_DIV_1 0b000
#define FRC_DIV_2 0b001

#define TIMER3_PERIOD_500KHZ 249
#define TIMER3_PERIOD_4MHZ 1999
#define TIMER3_PERIOD_8MHZ 3999
#define TIMER3_PERIOD_32MHZ 15999

static volatile uint16_t ms_ticks;
static volatile uint16_t half_second_ticks;

static void wait_for_uart_tx(void)
{
    if (U2MODEbits.UARTEN)
    {
        while (!U2STAbits.TRMT)
        {
        }
    }
}

static void switch_oscillator(uint8_t source, uint8_t frc_divider)
{
    CLKDIVbits.RCDIV = frc_divider;
    __builtin_write_OSCCONH(source);
    __builtin_write_OSCCONL(OSCCON | 0x01);
    while (OSCCONbits.OSWEN)
    {
    }
}

static void start_millisecond_timer(uint16_t period)
{
    TMR3 = 0;
    PR3 = period;
    IFS0bits.T3IF = 0;
    T3CONbits.TON = 1;
}

static void restore_uart(uint16_t baud_generator)
{
    U2MODEbits.BRGH = 1;
    U2BRG = baud_generator;
    U2MODEbits.UARTEN = 1;
    U2STAbits.UTXEN = 1;
}

uint16_t timer_millis(void)
{
    return ms_ticks;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    ms_ticks += 500;
    half_second_ticks++;
    if (half_second_ticks == 2)
    {
        half_second_ticks = 0;
        elapsed_value--;
    }
    IFS0bits.T2IF = 0;
}

void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    half_second_ticks = 0;
    ms_ticks++;
    IFS0bits.T3IF = 0;
}

void timer2_init(void)
{
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0x00;
    TMR2 = 0;
    PR2 = 7668;
    _T2IP = 4;
    _T2IF = 0;
    _T2IE = 1;
    T2CONbits.TON = 0;
}

void timer3_init(void)
{
    T3CON = 0;
    TMR3 = 0;
    PR3 = TIMER3_PERIOD_8MHZ;
    T3CONbits.TCKPS = 0b00;
    T3CONbits.TSIDL = 0;
    IPC2bits.T3IP = 2;
    IFS0bits.T3IF = 0;
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
}

void clock_set_31khz(void)
{
    wait_for_uart_tx();
    T3CONbits.TON = 0;
    switch_oscillator(OSC_LPRC, FRC_DIV_1);
    U2MODEbits.BRGH = 1;
    U2BRG = 12;
    U2MODEbits.USIDL = 0;
    TMR2 = 0;
    IFS0bits.T2IF = 0;
    T2CONbits.TON = 1;
}

void clock_set_500khz(void)
{
    wait_for_uart_tx();
    U2MODEbits.UARTEN = 0;
    T2CONbits.TON = 0;
    T3CONbits.TON = 0;
    switch_oscillator(OSC_LP_FRC, FRC_DIV_1);
    start_millisecond_timer(TIMER3_PERIOD_500KHZ);
}

void clock_set_4mhz(void)
{
    wait_for_uart_tx();
    T2CONbits.TON = 0;
    T3CONbits.TON = 0;
    switch_oscillator(OSC_FRC_DIV, FRC_DIV_2);
    restore_uart(0);
    start_millisecond_timer(TIMER3_PERIOD_4MHZ);
}

void clock_set_8mhz(void)
{
    wait_for_uart_tx();
    T2CONbits.TON = 0;
    T3CONbits.TON = 0;
    switch_oscillator(OSC_FRC, FRC_DIV_1);
    restore_uart(1);
    start_millisecond_timer(TIMER3_PERIOD_8MHZ);
}

void clock_set_32mhz(void)
{
    wait_for_uart_tx();
    T2CONbits.TON = 0;
    T3CONbits.TON = 0;
    switch_oscillator(OSC_FRC_PLL, FRC_DIV_1);
    while (!OSCCONbits.LOCK)
    {
    }
    restore_uart(7);
    start_millisecond_timer(TIMER3_PERIOD_32MHZ);
}

void clock_idle_31khz(void)
{
    uint8_t previous_source = OSCCONbits.COSC;
    uint8_t previous_frc_divider = CLKDIVbits.RCDIV;

    clock_set_31khz();
    Idle();

    switch (previous_source)
    {
    case OSC_FRC:
        clock_set_8mhz();
        break;
    case OSC_FRC_PLL:
        clock_set_32mhz();
        break;
    case OSC_LPRC:
        break;
    case OSC_LP_FRC:
        clock_set_500khz();
        break;
    case OSC_FRC_DIV:
        if (previous_frc_divider == FRC_DIV_2)
        {
            clock_set_4mhz();
        }
        else
        {
            clock_set_8mhz();
        }
        break;
    default:
        clock_set_8mhz();
        break;
    }
}
