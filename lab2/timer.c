#include "timer.h"
#include "IOs.h"
#include <xc.h>




volatile uint16_t isr_flag = 0;

void delay_ms(uint16_t delay){
    if (delay > 500){
        T2CONbits.TCKPS = 2;
        PR2 = delay*2;}
    else{
        T2CONbits.TCKPS = 0;
        PR2 = delay*125;}
    TMR2 = 0;
    IEC0bits.T2IE = 1;
    isr_flag = 0;
    while(!isr_flag){
        Idle();
    }

}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    LED_TOGGLE_1;
    IEC0bits.T2IE = 0;
    IFS0bits.T2IF = 0;
    isr_flag = 1;
}
void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    LED_TOGGLE_2;
    IFS0bits.T3IF = 0;
}


void timer2Init(void)
{

    PMD1bits.T2MD = 0;
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0;
    TMR2 = 0;
    PR2 = 6000;
    IFS0bits.T2IF = 0;
    T2CONbits.TON = 1;
}
void timer3Init(void)
{

    PMD1bits.T3MD = 0;
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0;
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0;
    TMR3 = 0;
    PR3 = 62500;
    IFS0bits.T3IF = 0;
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
}

