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
  static uint8_t pb_bitmask_old = 0b000;
  // We create a bitmask composed of each push button state to simplify state
  // changing logic
  uint8_t pb_bitmask = (pb3_flag << 2) | 
                       (pb2_flag << 1) | 
                       (pb1_flag);

  switch (pb_bitmask) {
    // Only PB1 is pressed
    case 0b001:
      if (pb_bitmask_old != 0b001) {
        printf("PB1 is pressed\n");
      }
      _LATB9 = ~_LATB9;
      delay_ms(250);
      break;

    // Only PB2 is pressed
    case 0b010:
      if (pb_bitmask_old != 0b010) {
        printf("PB2 is pressed\n");
      }
      _LATB9 = ~_LATB9;
      delay_ms(1000);
      break;

    // Only PB3 is pressed
    case 0b100:
      if (pb_bitmask_old != 0b100) {
        printf("PB3 is pressed\n");
      }
      _LATB9 = ~_LATB9;
      delay_ms(6000);
      break;

    // More than one button is pressed
    case 0b011:
    case 0b101:
    case 0b110:
      _LATB9 = 1;
      uint8_t first = pb1_flag ? 1 : 2;
      uint8_t second = pb3_flag ? 3 : 2;
      if (pb_bitmask_old != ((1 << (first - 1)) | ((1 << (second - 1))))) {
        printf("PB%d and PB%d are pressed\n", first, second);
      }
      break;

    // All buttons pressed
    case 0b111:
      _LATB9 = 1;
      if (pb_bitmask_old != 0b111) {
        printf("All PBs pressed\n");
      }
      break;

    // No buttons are pressed
    default:
    case 0b000:
      _LATB9 = 0;
      break; 
  }

  pb_bitmask_old = pb_bitmask;
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void){
  _CNIF = 0;

  if (_RB7 == 1) {
    pb1_flag = 1;
  }
  else {
    pb1_flag = 0;
  }

  if (_RB4 == 1) {
    pb2_flag = 1;
  }
  else {
    pb2_flag = 0;
  }

  if (_RA4 == 1) {
    pb3_flag = 1;
  }
  else {
    pb3_flag = 0;
  }
}
