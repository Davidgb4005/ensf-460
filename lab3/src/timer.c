#include "timer.h"
#include "IOs.h"
#include <xc.h>


static volatile uint16_t timer2_flag = 0;

static volatile uint16_t s_ticks;

void delay_ms(uint16_t ms) {
  //T2CONbits.TON = 0;

  //if (ms > 500){
  //  T2CONbits.TCKPS = 2;
  //  PR2 = ms * 2;
  //}

  //else {
  //  T2CONbits.TCKPS = 0;
  //  PR2 = ms * 125;
  //}
  //TMR2 = 0;
  //IEC0bits.T2IE = 1;
  //timer2_flag = 0;

  //T2CONbits.TON = 1;

  //while(!timer2_flag){
  //  Idle();
  //}

  uint16_t start = s_ticks;
  while((uint16_t)(s_ticks - start) < ms) {
    Idle();
  };
}

void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void) {
  //LED_TOGGLE_1;
  //IEC0bits.T2IE = 0;
  IFS0bits.T2IF = 0;
  timer2_flag = 1;
  s_ticks++;
}

void timer2_init(void) {
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
}
