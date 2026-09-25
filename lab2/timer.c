#include "timer.h"

#include <xc.h>




#define ONE        0
#define EIGHT      1
#define SIXFOUR    2
#define TWOFIVESIX 3

#define PRESCALER EIGHT
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



static volatile uint16_t timer1_periods;
static uint16_t timer1_period = TIMER_PERIOD_MAX;

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


static uint32_t timer1_ticks_snapshot(void)
{
    uint16_t periods;
    uint16_t count;
    uint8_t t1ie;
    t1ie = IEC0bits.T1IE;
    IEC0bits.T1IE = 0;
    periods = timer1_periods;
    count = TMR1;
    if (IFS0bits.T1IF != 0u) {
        periods++;
        count = TMR1;
    }
    IEC0bits.T1IE = t1ie;
    return ((uint32_t)periods * ((uint16_t)timer1_period + 1u)) + count;
}

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void)
{
    timer1_periods++;
    IFS0bits.T1IF = 0;
}

void timer1Init(void)
{

    PMD1bits.T1MD = 0;

    T1CONbits.TON = 0;
    T1CONbits.TCS = 0;
    T1CONbits.TSYNC = 0;
    T1CONbits.TGATE = 0;
    T1CONbits.TCKPS = PRESCALER;

    timer1_periods = 0;
    timer1_period = TIMER_PERIOD_MAX;

    TMR1 = 0;
    PR1 = timer1_period;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

uint32_t millis(void)
{
    return micros() / 1000u;
}

uint32_t micros(void)
{
    return timer1_ticks_snapshot()*PRESCALER_COE;
}



