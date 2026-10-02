#ifndef _PICSDK_UART_H
#define _PICSDK_UART_H

#include <stdint.h>
#include <stddef.h>

#include <drivers/io.h>


int uart_init(uint8_t channel, uint32_t baud_rate);

int uart_write(uint8_t channel, uint8_t tx);

int uart_get(uint8_t channel, uint8_t *rx);

int uart_writen(uint8_t channel, const uint8_t *tx, size_t n);

#endif
