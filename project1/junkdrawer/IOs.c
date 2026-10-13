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

typedef struct
{
    int16_t set_value;
    int16_t elapsed_value;
    union
    {
        uint16_t state;

        struct
        {
            uint16_t idle : 1;
            uint16_t start : 1;
            uint16_t pause : 1;
            uint16_t alarm : 1;
            uint16_t reset : 1;
            uint16_t clear : 1;
            uint16_t reserved : 10;
        } st_bits;
    };
    union
    {
        uint16_t request;

        struct
        {
            uint16_t start_request : 1;
            uint16_t run_request : 1;
            uint16_t pause_request : 1;
            uint16_t alarm_request : 1;
            uint16_t reset_request : 1;
            uint16_t clear_request : 1;
            uint16_t increment_request : 1;
            uint16_t fast_request : 1;
            uint16_t decrement_request : 1;
            uint16_t minute_request : 1;
            uint16_t info_request : 1;
            uint16_t flags_valid : 1;
            uint16_t reserved : 4;
        } rq_bits;
    };

} control_t;
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

void send_message(const char *msg, uint16_t cnt_value, uint16_t set_value)
{
    if (msg == NULL)
        return;

    const char *cursor = msg;
    char tmp_buffer[64];

    uint16_t value = (cursor == messages[COUNT_FORMAT])
                         ? cnt_value
                         : set_value;

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
void increment_request_handler(control_t *params)
{
    int16_t magnitude = 0;
    if (params->rq_bits.increment_request)
    {
        magnitude = 1;
    }
    else if (params->rq_bits.decrement_request)
    {
        magnitude = -1;
    }

    if (params->rq_bits.fast_request)
    {
        magnitude *= 5;
    }

    if (params->rq_bits.minute_request)
    {
        magnitude *= 60;
    }
    params->set_value += magnitude;
    if (params->set_value > 5999)
    {
        params->set_value = 5999;
    }
    if (params->set_value < 0)
    {
        params->set_value = 0;
    }
    params->rq_bits.fast_request = 0;
    params->rq_bits.minute_request = 0;
    params->rq_bits.decrement_request = 0;
    params->rq_bits.increment_request = 0;
    params->state = 0;
    params->st_bits.idle = 1;
    if (magnitude != 0)
    {
        send_message(messages[SET_FORMAT], 0, params->set_value);
    }
}
void start_request_handler(control_t *params)
{
    if (params->rq_bits.flags_valid && params->rq_bits.start_request)
    {
        params->elapsed_value = params->set_value;
        params->state = 0;
        params->st_bits.start = 1;
    }
    params->rq_bits.start_request = 0;
}
void alarm_request_handler(control_t *params)
{
    if (params->rq_bits.alarm_request)
    {
        params->state = 0;
        params->st_bits.alarm = 1;
        send_message(messages[ALARM_FORMAT], 0, 0);
    }
    params->rq_bits.alarm_request = 0;
}
void pause_request_handler(control_t *params)
{
    if (params->rq_bits.flags_valid && params->rq_bits.pause_request)
    {
        params->state = 0;
        params->st_bits.pause = !params->st_bits.pause;
        params->st_bits.start = !params->st_bits.pause;
    }
    params->rq_bits.pause_request = 0;
}
void clear_request_handler(control_t *params)
{
    if (params->rq_bits.clear_request)
    {
        params->state = 0;
        params->st_bits.idle = 1;
        send_message(messages[CLEAR_FORMAT], 0, 0);
    }
    params->rq_bits.clear_request = 0;
}

void reset_request_handler(control_t *params)
{
    if (params->rq_bits.reset_request)
    {
        params->set_value = 0;
        send_message(messages[SET_FORMAT], 0, 0);
    }
    params->rq_bits.reset_request = 0;
}
void info_request_handler(control_t *params)
{
    if (params->rq_bits.info_request)
    {
        send_message(messages[INFO_FORMAT], 0, 0);
    }
    params->rq_bits.info_request = 0;
}
void IOcheck(void)
{
    const char *selected_message = NULL;
    static void (*state_invocation)(void *) = NULL;
    static control_t control_register = {
        .st_bits.idle = 1};
    static uint16_t prev_request_register;
    static uint16_t delay = 0;
    static uint16_t prev = 0;
    static uint16_t prev_delay = 0;
    uint16_t released = 0;
    static int16_t hold;
    static int16_t hold_delay = 3000;
    static uint16_t debounce_complete = 0;
    static uint16_t debounce_delay = 125;
    static uint16_t debounce_elapsed = 0;
    static uint8_t prev_pb_bitmask = 0xff;
    static uint8_t pb_state = 0;
    static uint8_t prev_pb_state = 0;
    static uint16_t prev_handler_delay = 0;
    static uint16_t handler_delay = 400;

    if (prev_pb_bitmask != pb_bitmask)
    {
        debounce_elapsed = millis();
        debounce_complete = 0;
        prev_pb_bitmask = pb_bitmask;
    }
    if (millis() - debounce_elapsed > debounce_delay && !debounce_complete)
    {
        debounce_complete = 1;
        released = pb_state & (pb_state ^ prev_pb_bitmask);
        pb_state = prev_pb_bitmask;
        prev_delay = millis();
        prev_handler_delay = delay+1;
        control_register.request = 0;
        hold = 0;
    }

    control_register.rq_bits.flags_valid |= (released > 0);
    switch (pb_state | released)
    {
    case 0b010:
        control_register.rq_bits.minute_request = control_register.st_bits.idle;
    case 0b001:
        control_register.rq_bits.increment_request = control_register.st_bits.idle | control_register.st_bits.alarm;
        control_register.rq_bits.fast_request = hold & control_register.st_bits.idle;
        break;
    case 0b011:
        control_register.rq_bits.start_request = (1 ^ hold) & control_register.st_bits.idle;
        control_register.rq_bits.reset_request = hold & control_register.st_bits.idle;
        break;
    case 0b110:
        control_register.rq_bits.minute_request = control_register.st_bits.idle;
    case 0b101:
        control_register.rq_bits.decrement_request = control_register.st_bits.idle | control_register.st_bits.alarm;
        control_register.rq_bits.fast_request = hold & control_register.st_bits.idle;
        control_register.rq_bits.pause_request = (1 ^ hold) & (control_register.st_bits.start || control_register.st_bits.pause);
        control_register.rq_bits.clear_request = hold & (control_register.st_bits.start || control_register.st_bits.pause);
        break;
    case 0b111:
        control_register.rq_bits.info_request = control_register.st_bits.idle;
    case 0b100:
        control_register.rq_bits.pause_request = (1 ^ hold) & (control_register.st_bits.start || control_register.st_bits.pause);
        control_register.rq_bits.clear_request = hold & (control_register.st_bits.start || control_register.st_bits.pause);
        break;
    case 0b000:
    default:
        break;
    }
    control_register.rq_bits.alarm_request |= (!control_register.elapsed_value && control_register.st_bits.start);

    if (millis() - prev > delay)
    {
        if (control_register.elapsed_value > 0 && control_register.st_bits.start)
        {
            control_register.elapsed_value--;
            send_message(messages[COUNT_FORMAT], control_register.elapsed_value, 0);
        }
        prev = millis();
        _LATB9 ^= control_register.st_bits.start;
        _LATB9 &= !control_register.st_bits.idle;
        _LATB9 |= control_register.st_bits.alarm;
        /*
        _LATA7 ^= control_register.st_bits.alarm;
        _LATB7 &= !control_register.st_bits.idle;
        _LATB7 &= control_register.st_bits.start;
        */
        delay = 1000 * (control_register.st_bits.start + control_register.st_bits.idle) - (750 * control_register.st_bits.alarm);
    }
    if (millis() - prev_handler_delay > handler_delay)
    {
        prev_handler_delay = millis();
        increment_request_handler(&control_register);
        info_request_handler(&control_register);
        start_request_handler(&control_register);
        reset_request_handler(&control_register);
        pause_request_handler(&control_register);
        clear_request_handler(&control_register);
        alarm_request_handler(&control_register);
        control_register.rq_bits.flags_valid = 0;
    }
    if (millis() - prev_delay > hold_delay)
    {
        hold = 1;
    }
}
void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    pb_bitmask = (_RA4 << 2) |
                 (_RB4 << 1) |
                 (_RB7);
    _CNIF = 0;
}
