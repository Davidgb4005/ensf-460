#include <stdio.h>
#include <xc.h>

#include "IOs.h"
#include "timer.h"


static volatile uint8_t pb1_flag;
static volatile uint8_t pb2_flag;
static volatile uint8_t pb3_flag;

void IOinit(void) {
  AD1PCFG = 0xFFFF;

  // LED1 Initialization
  _TRISB9 = 0;

  // PB1 Initialization
  _TRISB7 = 1; 
  _CN23PUE = 0;
  _CN23PDE = 1;
  _CN23IE = 1;

  // PB2 Initialization
  _TRISB4 = 1;
  _CN1PUE = 0;
  _CN1PDE = 1;
  _CN1IE = 1;

  // PB3 Initialization
  _TRISA4 = 1;
  _CN0PUE = 0;
  _CN0PDE = 1;
  _CN0IE = 1;

  // Configure CN interrupts
  _CNIP = 6;
  _CNIF = 0;
  _CNIE = 1;
}

void IOcheck(void) {
  // We create a bitmask composed of each push button state to simplify state
  // changing logic
  uint8_t pb_bitmask = (pb3_flag << 2) | 
                       (pb2_flag << 1) | 
                       (pb1_flag);

  switch (pb_bitmask) {
    // Only PB1 is pressed
    case 0b001:
      printf("PB1 is pressed\n");
      delay_ms(250);
      pb1_flag = 0;
      break;

    // Only PB2 is pressed
    case 0b010:
      printf("PB2 is pressed\n");
      delay_ms(1000);
      pb2_flag = 0;
      break;

    // Only PB3 is pressed
    case 0b100:
      printf("PB3 is pressed\n");
      delay_ms(6000);
      pb3_flag = 0;
      break;

    // No buttons are pressed
    case 0b000:
      break;

    // More than one button is pressed
    default:
      delay_ms(1);
      break;
  }
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void){
  _CNIF = 0;

  if (_RB7 == 1) {
    pb1_flag = 1;
  }

  if (_RB4 == 1) {
    pb2_flag = 1;
  }

  if (_RA4 == 1) {
    pb3_flag = 1;
  }
}
