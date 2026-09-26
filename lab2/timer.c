#include "timer.h"
#include "IOs.h"
#include <xc.h>



uint32_t led_1_period = 0;
uint32_t led_2_period = 0;
uint16_t led_1_delay = 0;
uint32_t timer_1_period = 0;
uint16_t enable = 0;



#if 1 //
void delay_ms(uint16_t delay){
        if(delay == 0){
            enable = 0;
            led_1_period = timer_1_period;
            return;
        }
        enable = 1;
        led_1_delay = (delay-1);
        Idle();
}


void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    timer_1_period++;
    if(enable && timer_1_period - led_1_period > led_1_delay){
        LED_TOGGLE_2;
        led_1_period = timer_1_period;
    }
    if(timer_1_period - led_2_period > 500){
        LED_TOGGLE_1;
        led_2_period = timer_1_period;
    }
    IFS0bits.T2IF = 0;
}


void timer2Init(void)
{

    PMD1bits.T2MD = 0;
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0;
    TMR2 = 0;
    PR2 = 125;
    IFS0bits.T2IF = 0;
    IEC0bits.T2IE = 1;
    T2CONbits.TON = 1;
}
#endif 
#if 0 //8mhz setup
void delay_ms(uint16_t delay){
        if(delay == 0){
            enable = 0;
            return;
        }
        enable = 1;
        led_1_delay = (delay-1);
        while(timer_1_period - led_1_period < led_1_delay){
            Idle();
        }
}


void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    timer_1_period++;
    if(enable && timer_1_period - led_1_period > led_1_delay){
        LED_TOGGLE_2;
        led_1_period = timer_1_period;
    }
    if(timer_1_period - led_2_period > 500){
        LED_TOGGLE_1;
        led_2_period = timer_1_period;
    }
    IFS0bits.T2IF = 0;
}


void timer2Init(void)
{

    PMD1bits.T2MD = 0;
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0;
    TMR2 = 0;
    PR2 = 2000;
    IFS0bits.T2IF = 0;
    IEC0bits.T2IE = 1;
    T2CONbits.TON = 1;
}

#endif
