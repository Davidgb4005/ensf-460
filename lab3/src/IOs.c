#include <stdio.h>
#include <xc.h>

#include "IOs.h"
#include "timer.h"

void IOinit(void)
{
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



volatile uint16_t cn_flag = 0;
static volatile uint8_t pb1_flag;
static volatile uint8_t pb2_flag;
static volatile uint8_t pb3_flag;
volatile uint8_t pb_bitmask;


const char *messages[8] = {
    "Nothing pressed\n",              // 0b000
    "PB1 is pressed\n",               // 0b001
    "PB2 is pressed\n",               // 0b010
    "PB1 and PB2 are pressed\n",      // 0b011
    "PB3 is pressed\n",               // 0b100
    "PB1 and PB3 are pressed\n",      // 0b101
    "PB2 and PB3 are pressed\n",      // 0b110
    "All PBs pressed\n"               // 0b111
};



void IOcheck(void)
{

  static uint8_t prev_pb_bitmask = 9;

  if (prev_pb_bitmask != pb_bitmask){
    uint8_t debounce_pb_mask = pb_bitmask;
    delay_ms(150);
    if(debounce_pb_mask == pb_bitmask){
      printf(messages[pb_bitmask]);
      prev_pb_bitmask = pb_bitmask;
    }
  }


  switch (pb_bitmask)
  {
  // Only PB1 is pressed
  case 0b001:
    _LATB9 = ~_LATB9;
    delay_ms(250);
    break;

  // Only PB2 is pressed
  case 0b010:
    _LATB9 = ~_LATB9;
    delay_ms(1000);
    break;

  // Only PB3 is pressed
  case 0b100:
    _LATB9 = ~_LATB9;
    delay_ms(3000);
    break;

  // More than one button is pressed
  case 0b011:
  case 0b101:
  case 0b110:
    _LATB9 = 1;
    break;
  // All buttons pressed
  case 0b111:
    _LATB9 = 1;
    break;

  // No buttons are pressed
  default:
  case 0b000:
    _LATB9 = 0;
    break;
  }



}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{

  pb_bitmask = (_RA4 << 2) | 
                (_RB4 << 1) | 
                (_RB7);
  cn_flag = 1;
  #if 0
  pb1_flag = _RB7;
  pb2_flag = _RB4;
  pb3_flag = _RA4;
  #endif
  _CNIF = 0;

}
