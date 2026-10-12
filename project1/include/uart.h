#ifndef UART_H
#define UART_H
#include <stdint.h>
#include "error.h"
#include "ring_buffer.h"


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

extern const char *messages[];

uint16_t uart_init(ring_buffer_s *uart_rx_buffer, ring_buffer_s *uart_tx_buffer);
error_e uart_write_char(uint8_t data);
error_e uart_write_string(const char *data);
uint8_t uart_read_char(void);
error_e uart_send_buffer(void);
error_e uart_int_to_ascii(uint16_t data, uint8_t *buffer);
error_e uart_send_message(const char *msg, uint16_t elapsed_value,uint16_t preset_value);
#endif
