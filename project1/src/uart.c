#include <xc.h>
#include <stddef.h>
#include <stdint.h>
#include "uart.h"

#define FCY 4000000UL
#define BAUD_RATE 500000

const char *const messages[] = {
    LINE_CLEAR("CNT %m : %s"),
    LINE_CLEAR("CNT %m : %s {PAUSED}"),
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %m : %s"),
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06"),
    LINE_CLEAR("CLR 00m : 00s")
};


static void uart_write_char(uint8_t data)
{
    while (U2STAbits.UTXBF)
    {
    }
    U2TXREG = data;
}

void uart_init(void)
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
}

void uart_write_string(const char *data)
{
    while (*data)
    {
        uart_write_char((uint8_t)*data++);
    }
    uart_write_char('\r');
    uart_write_char('\n');
}

void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
    if (U2STAbits.OERR)
    {
        U2STAbits.OERR = 0;
    }

    while (U2STAbits.URXDA)
    {
        uint8_t c = U2RXREG;
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
    }

    IFS1bits.U2RXIF = 0;
}

void uart_send_state(const test_rig_data_s *data)
{
    uint8_t gpio_flags = ((data->gpio.led_1_state & 0x03u) << 0) |
                         ((data->gpio.led_2_state & 0x03u) << 2) |
                         ((data->gpio.pb_1 & 0x01u) << 4) |
                         ((data->gpio.pb_2 & 0x01u) << 5) |
                         ((data->gpio.pb_3 & 0x01u) << 6);
    uint16_t state_flags = ((data->states.state_code & 0x1fu) << 0) |
                           ((data->states.prev_state_code & 0x1fu) << 5);
    uint8_t payload[7] = {
        (uint8_t)data->elapsed_seconds,
        (uint8_t)(data->elapsed_seconds >> 8),
        (uint8_t)data->preset_seconds,
        (uint8_t)(data->preset_seconds >> 8),
        gpio_flags,
        (uint8_t)state_flags,
        (uint8_t)(state_flags >> 8),
    };
    uint8_t checksum = 0;

    uart_write_char(0xAAu);
    uart_write_char(0x55u);

    for (size_t i = 0; i < sizeof(payload); i++)
    {
        uart_write_char(payload[i]);
        checksum = (uint8_t)(checksum + payload[i]);
    }
    uart_write_char(checksum);
}

void uart_send_message(const char *msg, uint16_t elapsed_value, uint16_t preset_value)
{
    static uint16_t prev_elapsed;
    static uint16_t prev_preset;
    static const char *prev_cursor;
    if (msg == NULL)
    {
        return;
    }
    if (prev_cursor == msg && prev_elapsed == elapsed_value && prev_preset == preset_value)
    {
        return;
    }
    const char *cursor = msg;
    char tmp_buffer[64];

    uint16_t value = (cursor == messages[UART_COUNT_FORMAT] || cursor == messages[UART_COUNT_PAUSED_FORMAT])
                         ? elapsed_value
                         : preset_value;

    uint16_t numbers[] = {value / 60, value % 60};

    size_t i = 0;
    size_t j = 0;
    size_t n = 0;

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
    prev_elapsed = elapsed_value;
    prev_preset = preset_value;
    prev_cursor = msg;
}
