#ifndef UART_H
#define UART_H
#include <stdint.h>

typedef struct
{
    uint16_t elapsed_seconds;
    uint16_t preset_seconds;
    struct
    {
        uint16_t led_1_state : 2;
        uint16_t led_2_state : 2;
        uint16_t pb_1 : 1;
        uint16_t pb_2 : 1;
        uint16_t pb_3 : 1;
        uint16_t reserved : 9;
    } gpio;
    struct
    {
        uint16_t state_code : 5;
        uint16_t prev_state_code : 5;
        uint16_t reserved : 6;
    } states;
} test_rig_data_s;
typedef enum
{
    UART_COUNT_FORMAT,
    UART_COUNT_PAUSED_FORMAT,
    UART_ALARM_FORMAT,
    UART_SET_FORMAT,
    UART_INFO_FORMAT,
    UART_CLEAR_FORMAT,
} uart_message_e;

#define LINE_CLEAR(x) "\r\033[2K" x

extern const char *const messages[];

void uart_init(void);
void uart_write_string(const char *data);
void uart_send_state(const test_rig_data_s *data);
void uart_send_message(const char *msg, uint16_t elapsed_value, uint16_t preset_value);
#endif
