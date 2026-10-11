#include <stdio.h>
#include "IOs.h"
#include "timer.h"
#include <xc.h>

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
    _TRISA4 = 1;
    _CN0PUE = 0;
    _CN0PDE = 1;
    _CN0IE = 1;

    // PB3 Initialization
    _TRISB4 = 1;
    _CN1PUE = 0;
    _CN1PDE = 1;
    _CN1IE = 1;

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
    LINE_CLEAR("CNT %dm : %ds"),
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %dm : %ds"),
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06"),
    LINE_CLEAR("CLR 00m : 00s")};

typedef union
{
    uint16_t value;

    struct
    {
        uint16_t pb1 : 1;
        uint16_t pb2 : 1;
        uint16_t pb3 : 1;
        uint16_t timer_start : 1;
        uint16_t alarm : 1;
        uint16_t timer_pause : 1;
        uint16_t long_hold : 1;
        uint16_t short_hold : 1;
        uint16_t reserved : 8;
    } bits;

} control_t;
typedef enum
{
    ADJUST_BY_SECOND = 1,
    ADJUST_BY_MINUTE = 60,
} modifier;

typedef struct
{
    control_t *pressed_state;
    control_t *current_state;
    int16_t *set_value;
    int16_t *cnt_value;
    modifier modifier;
    uint16_t time_base;
    uint16_t *count_prev;
    const char *selected_message;

} parameters;

void send_message(parameters *params)
{
    if (params->selected_message != NULL)
    {
        if (params->selected_message == messages[COUNT_FORMAT])
        {
            printf(params->selected_message, *(params->cnt_value) / 60, *(params->cnt_value) % 60);
        }
        else
        {
            printf(params->selected_message, *(params->set_value) / 60, *(params->set_value) % 60);
        }
    }
}

void timer_run(void *params)
{
    parameters *typed_params = (parameters *)params;

    if (*(typed_params->cnt_value) > 0)
    {
        _LATB9 = !_LATB9;
        --*(typed_params->cnt_value);
    }
    if (*(typed_params->cnt_value) == 0)
    {
        typed_params->pressed_state->bits.alarm = 1;
        _LATB9 = 1;
        _LATA7 = 0;
        typed_params->selected_message = messages[ALARM_FORMAT];
    }
    else
    {
        typed_params->selected_message = messages[COUNT_FORMAT];
    }
    send_message(typed_params);
}

void timer_alarm(void *params)
{
    _LATB9 = 1;
    _LATA7 = !_LATA7;
}
void timer_reset_alarm(parameters *typed_params)
{
    typed_params->pressed_state->bits.alarm = 0;
    _LATA7 = 0;
}
void increment_set(void *params)
{
    parameters *typed_params = (parameters *)params;
    int16_t value = *(typed_params->set_value);
    int16_t amount = typed_params->pressed_state->bits.long_hold ? 5 : 1;

    if (typed_params->pressed_state->bits.long_hold)
    {
        *(typed_params->set_value) += typed_params->modifier * 5;
    }
    else
    {
        *(typed_params->set_value) += typed_params->modifier;
    }

    *(typed_params->set_value) = value;
    typed_params->selected_message = messages[SET_FORMAT];
    send_message(typed_params);
}
void decrement_set(void *params)
{
    parameters *typed_params = (parameters *)params;
    int16_t value = *(typed_params->set_value);
    int16_t amount = typed_params->pressed_state->bits.long_hold ? 5 : 1;

    if (typed_params->pressed_state->bits.long_hold)
    {
        *(typed_params->set_value) -= typed_params->modifier * 5;
    }
    else
    {
        *(typed_params->set_value) -= typed_params->modifier;
    }

    *(typed_params->set_value) = value;
    timer_reset_alarm(typed_params)
        typed_params->selected_message = messages[SET_FORMAT];
    send_message(typed_params);
}

void timer_clear(void *params)
{
    parameters *typed_params = (parameters *)params;
    if (typed_params->pressed_state->bits.long_hold)
    {
        *(typed_params->cnt_value) = 0;
        *(typed_params->set_value) = 0;
        typed_params->pressed_state->bits.timer_pause = 0;
        typed_params->pressed_state->bits.timer_start = 0;
        typed_params->pressed_state->bits.alarm = 0;
        _LATB9 = 0;
        _LATA7 = 0;
        typed_params->selected_message = messages[CLEAR_FORMAT];
        send_message(typed_params);
    }
    else if (typed_params->pressed_state->bits.pb3 &&
             !typed_params->current_state->bits.pb3)
    {
        typed_params->pressed_state->bits.timer_pause ^= 1;
        *(typed_params->count_prev) = millis();
        typed_params->selected_message = messages[COUNT_FORMAT];
        send_message(typed_params);
    }
}

void timer_start(void *params)
{
    parameters *typed_params = (parameters *)params;
    if (typed_params->pressed_state->bits.long_hold)
    {
        *(typed_params->cnt_value) = 0;
        *(typed_params->set_value) = 0;
        typed_params->pressed_state->bits.timer_pause = 0;
        typed_params->pressed_state->bits.timer_start = 0;
        typed_params->pressed_state->bits.alarm = 0;
        typed_params->selected_message = messages[SET_FORMAT];
        send_message(typed_params);
    }
    else if ((!typed_params->current_state->bits.pb1 || !typed_params->current_state->bits.pb2) && *(typed_params->set_value) > 0)
    {
        *(typed_params->cnt_value) = *(typed_params->set_value);
        typed_params->pressed_state->bits.timer_start = 1;
        typed_params->pressed_state->bits.timer_pause = 0;
        typed_params->pressed_state->bits.alarm = 0;
        *(typed_params->count_prev) = millis();
        _LATB9 = 1;
        _LATA7 = 0;
        typed_params->selected_message = messages[COUNT_FORMAT];
        send_message(typed_params);
    }
}

void IOcheck(void)
{
    static control_t control_register;
    static control_t current_state;
    static int16_t cur_set_value = 0;
    static int16_t cur_cnt_value = 0;
    static uint16_t prev = 0;
    static uint16_t prev_delay = 0;
    static uint16_t count_prev = 0;
    static uint16_t alarm_prev = 0;
    static uint8_t long_handled = 0;

    static uint8_t debounce_complete = 0;
    static uint16_t debounce_elapsed = 0;
    static uint8_t prev_pb_bitmask = 0xff;
    const uint16_t debounce_delay = 150;
    uint8_t current = control_register.value & 0x07;
    uint8_t released = 0;
    uint8_t changed = 0;
    uint16_t now = millis();

    static parameters params = {
        .pressed_state = &control_register,
        .current_state = &current_state,
        .set_value = &cur_set_value,
        .cnt_value = &cur_cnt_value,
        .time_base = 200,
        .count_prev = &count_prev};
    parameters *params_ptr = &params;
    static void (*state_invocation)(void *) = NULL;

    if (prev_pb_bitmask != pb_bitmask)
    {
        debounce_elapsed = now;
        debounce_complete = 0;
        prev_pb_bitmask = pb_bitmask;
    }
    if ((uint16_t)(now - debounce_elapsed) >= debounce_delay && !debounce_complete)
    {
        debounce_complete = 1;
        released = current & (current ^ prev_pb_bitmask);
        changed = current ^ prev_pb_bitmask;
        current_state = control_register;
        current_state.value = (current_state.value & ~0x07) | prev_pb_bitmask;

        // Handle the release in the previous state before selecting the new state.
        if (released && !long_handled && (uint16_t)(now - prev_delay) < 3000)
        {
            if (current == 0b011 || (current == 0b100 && control_register.bits.timer_start))
            {
                if (current == 0b011)
                    timer_start(params_ptr);
                else
                    timer_clear(params_ptr);
            }
        }

        control_register.value = (control_register.value & ~0x07) | prev_pb_bitmask;
        control_register.bits.short_hold = 0;
        control_register.bits.long_hold = 0;
        long_handled = 0;
        prev_delay = prev = now;
    }

    current = control_register.value & 0x07;
    current_state = control_register;

    // Determine hold thresholds without including them in the switch value.
    if (current && (uint16_t)(now - prev_delay) >= 250)
        control_register.bits.short_hold = 1;
    if (current && (uint16_t)(now - prev_delay) >= 1500)
        control_register.bits.long_hold = 1;

    // A long hold is a one-shot action, not a repeating callback.
    if (!long_handled && (uint16_t)(now - prev_delay) >=
                             ((current == 0b100 && control_register.bits.timer_start) ? 3000 : 2000))
    {
        if (current == 0b011 && !control_register.bits.timer_start)
        {
            control_register.bits.long_hold = 1;
            timer_start(params_ptr);
            long_handled = 1;
        }
        else if (current == 0b100 && control_register.bits.timer_start)
        {
            control_register.bits.long_hold = 1;
            timer_clear(params_ptr);
            long_handled = 1;
        }
    }

    // The callback is selected from the button bits, running bit and alarm bit.
    switch (control_register.value & 0x1F)
    {
    case 0b00001:
        state_invocation = increment_set;
        params.modifier = ADJUST_BY_SECOND;
        break;
    case 0b00010:
        state_invocation = increment_set;
        params.modifier = ADJUST_BY_MINUTE;
        break;
    case 0b00101:
        state_invocation = decrement_set;
        params.modifier = ADJUST_BY_SECOND;
        break;
    case 0b00110:
        state_invocation = decrement_set;
        params.modifier = ADJUST_BY_MINUTE;
        break;
    case 0b00111:
        state_invocation = NULL;
        if (changed)
        {
            params.selected_message = messages[INFO_FORMAT];
            send_message(params_ptr);
        }
        break;
    default:
        state_invocation = NULL;
        break;
    }

    if (changed)
        prev = now;

    if (state_invocation != NULL &&
        (uint16_t)(now - prev) >= params.time_base)
    {
        prev += params.time_base;
        state_invocation(params_ptr);
    }

    if (control_register.bits.timer_start && !control_register.bits.timer_pause &&
        !control_register.bits.alarm && (uint16_t)(now - count_prev) >= 1000)
    {
        count_prev += 1000;
        timer_run(params_ptr);
    }

    if (control_register.bits.alarm && (uint16_t)(now - alarm_prev) >= 250)
    {
        alarm_prev = now;
        timer_alarm(params_ptr);
    }

    Idle();
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    pb_bitmask = (_RB4 << 2) |
                 (_RA4 << 1) |
                 (_RB7);
    _CNIF = 0;
}
