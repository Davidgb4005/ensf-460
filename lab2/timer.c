#include "timer.h"

#include <xc.h>




#define ONE        0
#define EIGHT      1
#define SIXFOUR    2
#define TWOFIVESIX 3

#define PRESCALER ONE 
#if PRESCALER == 0
    #define PRESCALER_COE 0.25
#elif PRESCALER == 1
    #define PRESCALER_COE 2
#elif PRESCALER == 2
    #define PRESCALER_COE 16
#elif PRESCALER == 3
    #define PRESCALER_COE 64
#else
    #error "Invalid prescaler"
#endif
#define TIMER_PERIOD_MAX 0xfffeu



static volatile uint16_t timer2_periods;
static uint16_t timer2_period = TIMER_PERIOD_MAX;

uint16_t TON_timer(timer_delay * delay_struct){
    if(delay_struct->IN && !delay_struct->reset){
        if(millis() - delay_struct->ET > delay_struct->PT){
            delay_struct->Q = 1;
        }
    }
    else{
        delay_struct->ET = millis(); 
        delay_struct->Q = 0;
        delay_struct->reset = 0;
    }
    return delay_struct->Q;
}
uint16_t TON_timer_micro(timer_delay * delay_struct){
    if(delay_struct->IN && !delay_struct->reset){
        if(micros() - delay_struct->ET > delay_struct->PT){
            delay_struct->Q = 1;
        }
    }
    else{
        delay_struct->ET = micros(); 
        delay_struct->Q = 0;
        delay_struct->reset = 0;
    }
    return delay_struct->Q;
}
uint16_t TOF_timer(timer_delay * delay_struct){
    if(delay_struct->IN){
        delay_struct->Q = 1;
        delay_struct->ET = millis(); 
    }
    else{
        if(millis() - delay_struct->ET > delay_struct->PT){
            delay_struct->Q = 0;
        }
    }
    return delay_struct->Q;
}


static uint32_t timer2_ticks_snapshot(void)
{
    uint16_t periods;
    uint16_t count;
    uint8_t t2ie;
    t2ie = IEC0bits.T2IE;
    IEC0bits.T2IE = 0;
    periods = timer2_periods;
    count = TMR2;
    if (IFS0bits.T2IF != 0u) {
        periods++;
        count = TMR2;
    }
    IEC0bits.T2IE = t2ie;
    return ((uint32_t)periods * ((uint16_t)timer2_period + 1u)) + count;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    timer2_periods++;
    IFS0bits.T2IF = 0;
}

void timer2Init(void)
{

    PMD1bits.T2MD = 0;

    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = PRESCALER;

    timer2_periods = 0;
    timer2_period = TIMER_PERIOD_MAX;

    TMR2 = 0;
    PR2 = timer2_period;
    IFS0bits.T2IF = 0;
    IEC0bits.T2IE = 1;
    T2CONbits.TON = 1;
}

uint32_t millis(void)
{
    return micros() / 1000u;
}

uint32_t micros(void)
{
    return timer2_ticks_snapshot()*PRESCALER_COE;
}



