#include "uart.h"

#include <xc.h>
#include "rx_buffer.h"
#include "system.h"

#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>


static volatile rx_buffer_s rx_buf;

/**
 * Calculates and returns the BRG value for a given baud rate.
 *
 * Uses global system clock frequency.
 */
static uint16_t uart_calculate_brg(uint32_t baud);

void uart_init(uint32_t baud) { 
    uart_disable();

    // Default to 8-bit data, no parity, one stop bit
    U2MODEbits.PDSEL = 0x0;
    U2MODEbits.STSEL = 0;

    // Use low baud rate mode
    U2MODEbits.BRGH = 0;
    U2BRG = uart_calculate_brg(baud);

    // Enable receive interrupt
    U2STAbits.URXISEL = 00;
    _U2RXIE = 1;

    uart_enable();
}

void uart_update_baud(uint32_t baud) { 
    uart_disable();

    U2BRG = uart_calculate_brg(baud);

    uart_enable();
}

void uart_disable(void) {
    // Disable transmission
    U2STAbits.UTXEN = 0;

    // Disable UART interface
    U2MODEbits.UARTEN = 0;
}

void uart_enable(void) {
    // Enable UART interface
    U2MODEbits.UARTEN = 1;

    // Enable transmission
    U2STAbits.UTXEN = 1;
}

void uart_write(uint8_t tx) {
    // Wait for the transmission buffer to hav a free slot
    while (U2STAbits.UTXBF) (void)0;

    U2TXREG = tx;

    // Wait until transmission is complete
    while (!U2STAbits.TRMT) (void)0;
}

int uart_get(uint8_t *rx) {
    int ret;

    ret = rx_buffer_get(&rx_buf, rx);

    return ret;
}

void uart_writen(const uint8_t *tx, size_t n) {
    for (size_t i = 0; i < n; i++) { 
        uart_write(tx[i]);
    }
}

void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void) {
    _U2RXIF = 0;

    // Wait for data to be available
    while (U2STAbits.URXDA) {
        rx_buffer_put(&rx_buf, U2RXREG);
    }

    // Clear overrun error
    if (U2STAbits.OERR) {
        U2STAbits.OERR = 0;
    }
}

int __attribute__((__section__(".libc.write")))
write(int file, char *ptr, int len) {
    (void)file;

    for (int i = 0; i < len; i++) {
        if (ptr[i] == '\n') {
            uart_write('\r');
        }

        uart_write(ptr[i]);
    }

    return len;
}

static uint16_t uart_calculate_brg(uint32_t baud) {
    const uint32_t freq = ((uint32_t)core_clock_khz * 1000) / 2;
    const uint16_t brg = (uint16_t)((uint32_t)freq / (16UL * baud)) - 1;

    return brg;
}
