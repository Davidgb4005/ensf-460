#ifndef UART_H
#define UART_H
#include <stdint.h>
#include "ringBuffer.h"



void uart_init(ring_buffer * uart_rx_buffer,ring_buffer * uart_tx_buffer);
void uart_write_char(uint8_t data);
void uart_write_string(uint8_t *data);
uint8_t uart_read_char(void);
void uart_send_buffer(void);
void intToAcsii(uint16_t data,uint8_t * buffer);
#endif
