#include <stdio.h>
#include <xc.h>

#include "IOs.h"
#include "timer.h"

void IOinit(void)
{
  AD1PCFG = 0xFFFF;

  // LED1 Initialization
  _TRISB9 = 0;
  // LED2 Initialization
  _TRISA7 = 0;
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

volatile uint8_t pb_bitmask;

typedef enum
{
  COUNT_FORMAT,
  FIN_FORMAT,
  SET_FORMAT,
  INFO_FORMAT,
  CLEAR_FORMAT,
} message_e;
#define LINE_CLEAR "\r\033[2K"

const char *messages[5] = {
    LINE_CLEAR("CNT %dm : %ds"), // 0b000
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %dm : %ds"),               // 0b001
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06") // 0b111
    LINE_CLEAR("CLR 00m : 00s"),               // 0b001

    LINE_CLEAR()};

inline uint8_t delay_checker(uint8_t short_delay_elapsed, uint8_t long_delay_elapsed)
{
  if (long_delay_elapsed && short_delay_elapsed)
  {
    return 5;
  }
  else if (short_delay_elapsed)
  {
    return 1;
  }
  return 0;
}

void IOcheck(void)
{

  const char *selected_message = NULL;

  static uint8_t timer_start = 0;
  static uint16_t timer_prev = 0;
  static uint16_t timer_pause = 0;
  static uint8_t control_register;
  uint8_t released = 0;
  uint16_t blink_delay = 1000;

  static int16_t second_set = 0;
  static int16_t second_cnt = 0;

  static uint16_t long_hold_delay = 250;
  static uint16_t short_hold_delay = 1500;
  static uint16_t short_delay_prev_time = 0;
  static uint16_t long_delay_prev_time = 0;
  static uint8_t short_delay_reset = 0;
  static uint8_t long_delay_reset = 0;
  uint8_t debounce_changed = 0;
  static uint8_t short_delay_elapsed = 0;
  static uint8_t long_delay_elapsed = 0;

  static uint8_t debounce_complete = 0;
  static uint16_t debounce_delay = 150;
  static uint8_t prev_pb_bitmask = 17;
  static uint8_t debounced_pb_bitmask = 0;
  uint16_t led_2_xor_mask = 0;
  uint16_t led_1_xor_mask = 1;
  if (prev_pb_bitmask != pb_bitmask)
  {
    debounce_complete = 0;
    debounce_elapsed = millis();
    prev_pb_bitmask = pb_bitmask;
  }
  if (millis() - debounce_elapsed > debounce_delay && !debounce_complete)
  {
    debounce_complete = 1;
    debounce_changed = 1;
    timer_prev = millis();
    released = debounced_pb_bitmask & (debounced_pb_bitmask ^ prev_pb_bitmask);
    debounced_pb_bitmask = prev_pb_bitmask;
  }

  switch (control_register)
  {

  // Paused States
  case 0b0001: // Second inc
    short_delay_reset = 1;
    second_set += delay_checker(short_delay_elapsed, long_delay_elapsed);
    selected_message = messages[SET_FORMAT];
    break;

  case 0b0010: // Min inc
    short_delay_reset = 1;
    second_set += delay_checker(short_delay_elapsed, long_delay_elapsed) * 60;
    selected_message = messages[SET_FORMAT];
    break;

  case 0b0011: // Start Request // Reset
    short_hold_delay = 0;
    long_hold_delay = 2000;
    if (long_delay_elapsed)
    {
      second_set = 0;
      selected_message = messages[SET_FORMAT];
    }
    else if (short_delay_elapsed && (released & control_register))
    {
      timer_start = 1;
      second_cnt = second_set;
      selected_message = messages[COUNT_FORMAT];
    }
    break;

  case 0b0101: // Second Dec
    short_delay_reset = 1;
    second_set -= delay_checker(short_delay_elapsed, long_delay_elapsed);
    selected_message = messages[SET_FORMAT];
    break;

  case 0b0110: // Min Dec
    short_delay_reset = 1;
    second_set -= delay_checker(short_delay_elapsed, long_delay_elapsed) * 60;
    selected_message = messages[SET_FORMAT];
    break;

  // All buttons pressed
  case 0b0111: // Info Requested
    selected_message = messages[INFO_FORMAT];
    break;

  default:
  case 0b0000:
    //selected_message = messages[CLEAR_FORMAT];
    selected_message = NULL;
    T2CONbits.TON = 0;
    Idle();
    T2CONbits.TON = 1;
    break;

    // Running States
  case 0b1100: // Pause // Clear
    short_hold_delay = 0;
    long_hold_delay = 3000;
    selected_message = messages[COUNT_FORMAT];
    if (long_delay_elapsed)
    {
      second_set = 0;
      second_cnt = 0;
      timer_start = 0;
      timer_pause = 0;
      selected_message = messages[CLEAR_FORMAT];
    }
    else if (short_delay_elapsed && (released & control_register))
    {
      timer_pause ^= 1;
    }

    break;

  case 0b1000:
    selected_message = messages[COUNT_FORMAT];
    if (second_cnt == 0)
    {
      selected_message = messages[FIN_FORMAT];
      led_2_xor_mask = 1;
      led_1_xor_mask = 0;
      _LATB9 = 1;
      blink_delay = 250;
    }
    break;
  }

  control_register = timer_start << 3 | debounced_pb_bitmask;

  if ((short_delay_reset&&short_delay_elapsed) || debounce_changed)
  {
    short_delay_prev_time = millis();
    short_delay_reset = 0;
  }
  if ((long_delay_reset&&long_delay_elapsed) || debounce_changed)
  {
    long_delay_prev_time = millis();
    long_delay_reset = 0;
  }

  if (millis() - short_delay_prev_time > short_hold_delay)
  {
    short_delay_elapsed = 1;
  }
  else
  {
    short_delay_elapsed = 0;
  }

  if (millis() - long_delay_prev_time > long_hold_delay)
  {
    long_delay_elapsed = 1;
  }
  else
  {
    long_delay_elapsed = 0;
  }

  if ((millis() - timer_prev) > blink_delay && timer_start && !timer_pause)
  {
    timer_prev = millis();
    _LATB9 ^= led_1_xor_mask;
    _LATA7 ^= led_2_xor_mask;
    if (second_cnt != 0)
    {
      second_cnt--;
    }
  }
  if (second_set < 0){
    second_set = 0;
  }
  if (selected_message != NULL)
  {
      if (selected_message == messages[COUNT_FORMAT])
      {
          printf(selected_message, second_cnt / 60, second_cnt % 60);
      }
      else
      {
          printf(selected_message, second_set / 60, second_set % 60);
      }
  }
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
  pb_bitmask = (_RA4 << 2) |
               (_RB4 << 1) |
               (_RB7);

  _CNIF = 0;
}
