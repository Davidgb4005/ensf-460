/**
 * This module enables the usage of the UART2 interface.
 *
 * Along with the following functions, this module also enables the use of stdio
 * functions, like printf(). Note that uart_init() must be called prior to the
 * first call of printf().
 */

#ifndef _UART_H
#define _UART_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>


/**
 * Intializes the UART2 interface with the given baud rate.
 *
 * The interface is configured for synchronous, blocking transmission, and
 * interrupt based receiving.
 */
void uart_init(uint32_t baud);

/**
 * Used to update the baud rate while the UART2 interface is already running.
 *
 * Must be called if the system core clock is ever changed.
 */
void uart_update_baud(uint32_t baud);

/**
 * Disables the UART2 interface.
 */
void uart_disable(void);

/**
 * Enables the UART2 interface.
 */
void uart_enable(void);

/**
 * Transmits a single byte over the UART2 interface.
 */
void uart_write(uint8_t tx);

/**
 * Transmits n bytes over the UART2 interface.
 */
void uart_writen(const uint8_t *tx, size_t n);

/**
 * Receives one byte from the internal RX buffer.
 *
 * Returns a negative errno on failure, or 0 on success.
 */
int uart_get(uint8_t *rx);


/**
 * Wrappers
 */

static inline void uart_write_char(char c) {
    uart_write((uint8_t)c);
}

static inline void uart_write_string(const char *str) {
    uart_writen((uint8_t *)str, strlen(str));
}

#define debug_print(fmt, ...) do {          \
    printf("%s:%d: ", __FILE__, __LINE__);  \
    printf((fmt), ##__VA_ARGS__);           \
} while (0)

#endif
