#include <xc.h>
#include <stdint.h>
#include "uart.h"
#include "stdlib.h"
#include "stdio.h"

#define FCY         2000000UL
#define BAUD_RATE   9600UL
#define ECHO_ENABLE 1

ring_buffer * rx_buffer;
ring_buffer * tx_buffer;


void uart_init(ring_buffer * uart_rx_buffer,ring_buffer * uart_tx_buffer)
{
    U1MODE = 0;
    U1STA = 0;
    U1BRG = (FCY / (16UL * BAUD_RATE)) - 1;
    U1STAbits.URXISEL = 0;
    IFS0bits.U1RXIF = 0;
    IEC0bits.U1RXIE = 1;
    U1MODEbits.UARTEN = 1;
    U1STAbits.UTXEN = 1;
    rx_buffer = uart_rx_buffer;
    tx_buffer = uart_tx_buffer;
}

void intToAcsii(uint16_t data,uint8_t * buffer){
    sprintf(buffer, "%u", data);
}

void uart_write_char(uint8_t data)
{
    while (U1STAbits.UTXBF);
    U1TXREG = data;
}

void uart_write_string(uint8_t *data)
{
    while (*data)
    {
        uart_write_char(*data++);
    }
    uart_write_char('\r');
    uart_write_char('\n');
}

uint8_t uart_read_char(void)
{
    while (!dataAvailable(rx_buffer));
    return dequeueChar(rx_buffer);
}

void uart_send_buffer(void)
{
    while (dataAvailable(tx_buffer))
    {
        uart_write_char(dequeueChar(tx_buffer));
    }
}

#if 1
void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void)
{
    if (U1STAbits.OERR)
    {
        U1STAbits.OERR = 0;
    }

    while (U1STAbits.URXDA)
    {
        uint8_t c = U1RXREG;
        enqueueChar(rx_buffer, c);

    #if ECHO_ENABLE
        if (!U1STAbits.UTXBF)
        {
            if(c == '\r'){
                U1TXREG = c;
                U1TXREG = '\n';
            }
            else{
                U1TXREG = c;
            }

        }
    #endif
    }

    IFS0bits.U1RXIF = 0;
}
#endif
#if 0
void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void)
{
    while (U1STAbits.URXDA)
    {
        uint8_t c = U1RXREG;

        while (U1STAbits.UTXBF);
        U1TXREG = c;
    }

    IFS0bits.U1RXIF = 0;
}
#endif