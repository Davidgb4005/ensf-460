#include "timer.h"
#include <xc.h>
#include "superloop.h"

static volatile uint16_t ms_ticks;
static volatile uint16_t s_ticks;
uint16_t timer_millis(void){
  return ms_ticks;
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void) {
  ms_ticks++;
  if (state == TIMER_RUN){
    s_ticks++;
    if(s_ticks > 1000){
      elapsed_value--;
      s_ticks = 0;
    }
  }
  IFS0bits.T2IF = 0;
}

uint16_t timer2_init(void) {
  T2CONbits.TON = 0;
  T2CONbits.TCS = 0;
  T2CONbits.TGATE = 0;
  T2CONbits.TCKPS = 0x01;
  TMR2 = 0;
  PR2 = 499;
  _T2IP = 4;
  _T2IF = 0;
  _T2IE = 1;
  T2CONbits.TON = 1;
  return 0;
}
