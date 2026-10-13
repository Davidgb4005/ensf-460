#include <stdio.h>
#include "IOs.h"
#include "timer.h"
#include "uart.h"
#include <xc.h>

void IOinit(void)
{
    AD1PCFG = 0xFFFF;

    // LED1 Initialization
    _TRISB9 = 0;
    // LED2 Initialization

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
    ALARM_FORMAT,
    SET_FORMAT,
    INFO_FORMAT,
    CLEAR_FORMAT,
} message_e;
#define LINE_CLEAR(x) "\r\033[2K" x

const char *messages[5] = {
    LINE_CLEAR("CNT %m : %s"),
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %m : %s"),
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06"),
    LINE_CLEAR("CLR 00m : 00s")};

typedef union
{
    uint16_t state;
    struct
    {
        uint16_t start : 1;
        uint16_t run : 1;
        uint16_t pause : 1;
        uint16_t alarm : 1;
        uint16_t reset : 1;
        uint16_t clear : 1;
        uint16_t reserved : 10;
    } bits;

    uint16_t request;
    struct
    {
        uint16_t start_request : 1;
        uint16_t run_request : 1;
        uint16_t pause_request : 1;
        uint16_t alarm_request : 1;
        uint16_t reset_request : 1;
        uint16_t clear_request : 1;
        uint16_t reserved : 10;
    } bits;

} control_t;

typedef enum
{
    ADJUST_BY_SECOND = 1,
    ADJUST_BY_MINUTE = 60,
} modifier;

typedef struct
{
    control_t *state;
    uint16_t *set_value;
    uint16_t *cnt_value;
    modifier modifier;
    uint16_t *long_hold;
    uint16_t *short_hold;
    uint16_t time_base;
    const char *selected_message;

} parameters;

void send_message(parameters *params)
{
    if (params->selected_message == NULL)
        return;

    const char *cursor = params->selected_message;
    char tmp_buffer[64];

    uint16_t value = (cursor == messages[COUNT_FORMAT])
                         ? *(params->cnt_value)
                         : *(params->set_value);

    uint16_t numbers[] = {value / 60, value % 60};

    int i = 0;
    int j = 0;
    int n = 0;

    while (cursor[i] != '\0' && j < sizeof(tmp_buffer) - 3)
    {
        if (cursor[i] == '%' && n < 2)
        {
            uint16_t num = numbers[n++];

            if (num >= 10)
                tmp_buffer[j++] = '0' + num / 10;

            tmp_buffer[j++] = '0' + num % 10;
        }
        else
        {
            tmp_buffer[j++] = cursor[i];
        }

        i++;
    }

    tmp_buffer[j] = '\0';
    uart_write_string(tmp_buffer);
}

void timer_run(void *params)
{
    parameters *typed_params = (parameters *)params;

    if (*(typed_params->cnt_value) > 0)
    {
        _LATB9 = ~_LATB9;
        *(typed_params->cnt_value) -= 1;
    }
    else
    {
        typed_params->state->bits.timer_start = 0;
        typed_params->state->bits.alarm = 1;
    }
    typed_params->selected_message = messages[COUNT_FORMAT];
    send_message(typed_params);
}
void timer_start(void *params)
{
    parameters *typed_params = (parameters *)params;

    if (typed_params->state->bits.request_to_start)
    {
        typed_params->state->bits.timer_start = 1;
    }
    typed_params->state->bits.request_to_start = 0;
}
void timer_request_start(void *params)
{
    parameters *typed_params = (parameters *)params;
    if (*(typed_params->long_hold))
    {
        *(typed_params->cnt_value) = 0;
        *(typed_params->set_value) = 0;
        typed_params->state->bits.timer_start = 0;
        typed_params->selected_message = messages[SET_FORMAT];
        typed_params->state->bits.request_to_start = 0;
    }
    else if (*(typed_params->short_hold))
    {
        *(typed_params->cnt_value) = *(typed_params->set_value);
        typed_params->state->bits.request_to_start = 1;
        typed_params->selected_message = messages[COUNT_FORMAT];
    }
}

void timer_alarm(void *params)
{
    parameters *typed_params = (parameters *)params;
    _LATB9 = 1;
    //_LATA7 = ~_LATA7;
    typed_params->selected_message = messages[ALARM_FORMAT];
    send_message(typed_params);
}
void increment_set(void *params)
{
    parameters *typed_params = (parameters *)params;
    if (*(typed_params->long_hold))
    {
        *(typed_params->set_value) += typed_params->modifier * 5;
    }
    else
    {
        *(typed_params->set_value) += typed_params->modifier;
    }
    if (*(typed_params->set_value) > 5940)
    {
        *(typed_params->set_value) = 5999;
    }
    typed_params->state->bits.alarm = 0;
    typed_params->selected_message = messages[SET_FORMAT];
    send_message(typed_params);
}
void decrement_set(void *params)
{
    parameters *typed_params = (parameters *)params;
    if (*(typed_params->long_hold))
    {

        *(typed_params->set_value) -= typed_params->modifier * 5;
    }
    else
    {
        *(typed_params->set_value) -= typed_params->modifier;
    }
    if (*(typed_params->set_value) < 0)
    {
        *(typed_params->set_value) = 0;
    }
    typed_params->state->bits.alarm = 0;
    typed_params->selected_message = messages[SET_FORMAT];
    send_message(typed_params);
}

void timer_clear(void *params)
{
    parameters *typed_params = (parameters *)params;
    if (*(typed_params->long_hold))
    {
        *(typed_params->cnt_value) = 0;
        *(typed_params->set_value) = 0;
        typed_params->state->bits.timer_start = 0;
        typed_params->selected_message = messages[CLEAR_FORMAT];
        typed_params->state->bits.request_to_pause = 0;
    }
    else
    {
        typed_params->state->bits.request_to_pause = 1;
        typed_params->selected_message = messages[COUNT_FORMAT];
    }
    send_message(typed_params);
}
void timer_pause(void *params)
{
    parameters *typed_params = (parameters *)params;

    if (typed_params->state->bits.request_to_pause)
    {
        typed_params->state->bits.timer_pause = 1
    }
    typed_params->state->bits.request_to_pause = 0;
}

void print_info(void *params)
{

    parameters *typed_params = (parameters *)params;
    typed_params->selected_message = messages[INFO_FORMAT];
    send_message(typed_params);
}
void IOcheck(void)
{
    const char *selected_message = NULL;
    static void (*state_invocation)(void *) = NULL;
    static control_t control_register;
    static control_t prev_control_reg;
    uint16_t released = 0;
    uint16_t pressed = 0;
    static uint16_t current = 0;

    static int16_t cur_set_value = 0;
    static int16_t cur_cnt_value = 0;
    static int16_t short_hold;
    static int16_t long_hold;
    static int16_t short_hold_delay = 500;
    static int16_t long_hold_delay = 3000;

    static uint16_t prev = 0;
    static uint16_t prev_delay = 0;

    static uint16_t debounce_complete = 0;
    static uint16_t debounce_delay = 50;
    static uint16_t debounce_elapsed = 0;
    static uint8_t prev_pb_bitmask = 0xff;

    static parameters params = {
        .state = &control_register,
        .set_value = &cur_set_value,
        .cnt_value = &cur_cnt_value,
        .long_hold = &long_hold,
        .short_hold = &short_hold};
    parameters *params_ptr = &params;

    if (prev_pb_bitmask != pb_bitmask)
    {
        debounce_elapsed = millis();
        debounce_complete = 0;
        prev_pb_bitmask = pb_bitmask;
    }
    if (millis() - debounce_elapsed > debounce_delay && !debounce_complete)
    {
        debounce_complete = 1;
        current = prev_pb_bitmask;
        released = current & (current ^ prev_pb_bitmask);
        pressed = current ^ prev_pb_bitmask;
        prev_delay = prev = millis();
        long_hold = 0;
        short_hold = 0;
    }

    if (millis() - prev > (params.time_base) && state_invocation != NULL)
    {
        prev = millis();
        (*state_invocation)(params_ptr);
    }
    if (millis() - prev_delay > short_hold_delay)
    {
        short_hold = 1;
    }
    if (millis() - prev_delay > long_hold_delay)
    {
        long_hold = 1;
    }

    switch (control_register)
    {
    case constant expression:
        /* code */
        break;

    default:
        break;
    }
    switch (current)
    {
    case 0b001:
        state_invocation = increment_set;
        params.modifier = ADJUST_BY_SECOND;
        params.time_base = 250;
        break;
    case 0b010:
        state_invocation = increment_set;
        params.modifier = ADJUST_BY_MINUTE;
        params.time_base = 250;
        break;
    case 0b101:
        state_invocation = decrement_set;
        params.modifier = ADJUST_BY_SECOND;
        params.time_base = 250;
        break;
    case 0b110:
        state_invocation = decrement_set;
        params.modifier = ADJUST_BY_MINUTE;
        params.time_base = 250;
        break;
    case 0b011:
        state_invocation = timer_request_start;
        params.time_base = 500;
        break;

    case 0b01000000:
    case 0b01000001:
    case 0b01000010:
    case 0b01000011:
        state_invocation = timer_start;
        params.time_base = 0;
    case 0b0111:
        state_invocation = print_info;
        params.time_base = 500;
        break;
    case 0b1111:
    case 0b1110:
    case 0b1101:
    case 0b1100:
        state_invocation = timer_clear;
        params.time_base = 1000;
        break;
    case 0b1000:
    case 0b1010:
    case 0b1001:
    case 0b1011:
        state_invocation = timer_run;
        params.time_base = 1000;
        break;
    case 0b10000:
    case 0b10001:
    case 0b10010:
    case 0b10011:
        state_invocation = timer_alarm;
        params.time_base = 250;
        break;
    case 0b00100100:
    case 0b00100101:
    case 0b00100110:
    case 0b00100111:
    case 0b10000100:
    case 0b10000101:
    case 0b10000110:
    case 0b10000111:
        state_invocation = timer_pause;
        params.time_base = 0;
    default:
        state_invocation = NULL;
        break;
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
