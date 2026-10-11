#include <xc.h>
#include <stdint.h>
#include "uart.h"
#include "stdlib.h"
#include "stdio.h"

#define FCY         4000000UL
#define BAUD_RATE   57600UL
#define ECHO_ENABLE 1

ring_buffer *rx_buffer;
ring_buffer *tx_buffer;


const char *messages[5] = {
    LINE_CLEAR("CNT %m : %s"),
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %m : %s"),
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06"),
    LINE_CLEAR("CLR 00m : 00s")
};

uint16_t uart_init(ring_buffer *uart_rx_buffer, ring_buffer *uart_tx_buffer)
{
    U2MODE = 0;
    U2STA = 0;
    U2MODEbits.BRGH = 1;
    U2BRG = (FCY / (4UL * BAUD_RATE)) - 1;
    U2STAbits.URXISEL = 0;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;
    U2MODEbits.UARTEN = 1;
    U2STAbits.UTXEN = 1;
    rx_buffer = uart_rx_buffer;
    tx_buffer = uart_tx_buffer;
    return 0;
}

void intToAcsii(uint16_t data, uint8_t *buffer)
{
    sprintf((char *)buffer, "%u", data);
}

void uart_write_char(uint8_t data)
{
    while (U2STAbits.UTXBF);
    U2TXREG = data;
}

void uart_write_string(uint8_t *data)
{
    while (*data)
    {
        uart_write_char(*data++);
    }
    //uart_write_char('\r');
    //uart_write_char('\n');
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
void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
    if (U2STAbits.OERR)
    {
        U2STAbits.OERR = 0;
    }

    while (U2STAbits.URXDA)
    {
        uint8_t c = U2RXREG;
        enqueueChar(rx_buffer, c);


void send_message(const char *msg, uint16_t value)
{
    if (msg == NULL)
        return;

    const char *cursor = msg;
    char tmp_buffer[64];


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
#if ECHO_ENABLE
        if (!U2STAbits.UTXBF)
        {
            if (c == '\r')
            {
                U2TXREG = c;
                U2TXREG = '\n';
            }
            else
            {
                U2TXREG = c;
            }
        }
#endif
    }

    IFS1bits.U2RXIF = 0;
}
#endif