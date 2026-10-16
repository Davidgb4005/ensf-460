#include "timer.h"
#include <xc.h>
#include "superloop.h"
extern volatile uint16_t elapsed_time;
volatile uint16_t ms_ticks;
uint16_t timer_millis(void)
{
    return ms_ticks;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    ms_ticks += 1000;
    if (elapsed_value != 0){
    elapsed_value--;
    }
    IFS0bits.T2IF = 0;
}

void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    ms_ticks++;
    IFS0bits.T3IF = 0;
}

uint16_t timer2_init(void)
{

    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0x00;
    TMR2 = 0;
    PR2 = 14590;
    _T2IP = 4;
    _T2IF = 0;
    _T2IE = 1;
    T2CONbits.TON = 0;
    return 0;
}

uint16_t timer3_init(void)
{
    T3CON = 0;
    TMR3 = 0;
    PR3 = 3999; // 1 ms at FCY = 4 MHz (8 MHz FRC)
    T3CONbits.TCKPS = 0b00;
    T3CONbits.TSIDL = 0;
    IPC2bits.T3IP = 2;
    IFS0bits.T3IF = 0;
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
    return 0;
}
