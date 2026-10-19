#include "uart.h"

#include <stddef.h>
#include <xc.h>

#include "system.h"

#define BAUD_8_MHZ 500000UL
#define BAUD_1_MHZ 125000UL
#define BAUD_500_KHZ 62500UL
#define BAUD_32_KHZ 300UL

const char *const messages[] = {
    LINE_CLEAR("CNT %m : %s"),
    LINE_CLEAR("CNT %m : %s {PAUSED}"),
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %m : %s"),
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06"),
    LINE_CLEAR("CLR 00m : 00s"),
};

static uint16_t uart_initialized;
static uint16_t uart_enabled;
static uint32_t uart_baud;

static uint16_t uart_brg(uint16_t clock_khz, uint32_t baud)
{
    uint32_t instruction_hz = ((uint32_t)clock_khz * 1000UL) / 2UL;
    uint32_t divisor = 4UL * baud;

    if (baud == 0UL || instruction_hz <= divisor)
    {
        return 0u;
    }

    return (uint16_t)(((instruction_hz + (divisor / 2UL)) / divisor) - 1UL);
}

void uart_wait_for_tx(void)
{
    if (uart_enabled != 0u)
    {
        while (U2STAbits.TRMT == 0u)
        {
        }
    }
}

void uart_update_baud(uint32_t baud)
{
    uint16_t was_enabled;

    uart_baud = baud;
    if (uart_initialized == 0u || uart_enabled == 0u)
    {
        return;
    }

    uart_wait_for_tx();
    was_enabled = U2MODEbits.UARTEN;
    U2STAbits.UTXEN = 0;
    U2MODEbits.UARTEN = 0;
    U2MODEbits.BRGH = 1;
    U2BRG = uart_brg(core_clock_khz, baud);

    if (was_enabled != 0u)
    {
        U2MODEbits.UARTEN = 1;
        U2STAbits.UTXEN = 1;
    }
}

void uart_update_clock(uint16_t clock_khz)
{
    uint32_t baud;

    if (clock_khz >= 4000u)
    {
        baud = BAUD_8_MHZ;
    }
    else if (clock_khz >= 1000u)
    {
        baud = BAUD_1_MHZ;
    }
    else if (clock_khz >= 500u)
    {
        baud = BAUD_500_KHZ;
    }
    else
    {
        baud = BAUD_32_KHZ;
    }

    uart_update_baud(baud);
}

void uart_init(uint32_t baud)
{
    PMD1bits.U2MD = 0;
    U2MODE = 0;
    U2STA = 0;
    U2MODEbits.PDSEL = 0;
    U2MODEbits.STSEL = 0;
    U2MODEbits.BRGH = 1;
    U2BRG = uart_brg(core_clock_khz, baud);

    U2STAbits.URXISEL = 0;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;

    U2MODEbits.UARTEN = 1;
    U2STAbits.UTXEN = 1;
    uart_baud = baud;
    uart_initialized = 1u;
    uart_enabled = 1u;
}

void uart_shutdown(void)
{
    if (uart_enabled == 0u)
    {
        return;
    }

    uart_wait_for_tx();
    IEC1bits.U2RXIE = 0;
    U2STAbits.UTXEN = 0;
    U2MODEbits.UARTEN = 0;
    PMD1bits.U2MD = 1;
    uart_enabled = 0u;
}

void uart_wake(void)
{
    if (uart_initialized == 0u || uart_enabled != 0u)
    {
        return;
    }

    PMD1bits.U2MD = 0;
    U2MODE = 0;
    U2STA = 0;
    U2MODEbits.PDSEL = 0;
    U2MODEbits.STSEL = 0;
    U2MODEbits.BRGH = 1;
    U2BRG = uart_brg(core_clock_khz, uart_baud);

    U2STAbits.URXISEL = 0;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;

    U2MODEbits.UARTEN = 1;
    U2STAbits.UTXEN = 1;
    uart_enabled = 1u;
}

void uart_write_char(uint8_t data)
{
    uart_wake();
    while (U2STAbits.UTXBF != 0u)
    {
    }
    U2TXREG = data;
}

void uart_write_string(const char *data)
{
    if (data == NULL)
    {
        return;
    }

    while (*data != '\0')
    {
        uart_write_char((uint8_t)*data++);
    }
    uart_write_char('\r');
    uart_write_char('\n');
}

void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
    if (U2STAbits.OERR != 0u)
    {
        U2STAbits.OERR = 0;
    }

    while (U2STAbits.URXDA != 0u)
    {
        uint8_t data = (uint8_t)U2RXREG;
        if (U2STAbits.UTXBF == 0u)
        {
            U2TXREG = data;
        }
    }

    IFS1bits.U2RXIF = 0;
}

void uart_send_state(const test_rig_data_s *data)
{
    uint8_t payload[7];
    uint8_t checksum = 0u;
    uint8_t gpio_flags;
    uint16_t state_flags;
    size_t i;

    if (data == NULL)
    {
        return;
    }

    gpio_flags = (uint8_t)(((data->gpio.led_1_state & 0x03u) << 0) |
                           ((data->gpio.led_2_state & 0x03u) << 2) |
                           ((data->gpio.pb_1 & 0x01u) << 4) |
                           ((data->gpio.pb_2 & 0x01u) << 5) |
                           ((data->gpio.pb_3 & 0x01u) << 6));
    state_flags = (uint16_t)(((data->states.state_code & 0x1fu) << 0) |
                             ((data->states.prev_state_code & 0x1fu) << 5));

    payload[0] = (uint8_t)data->elapsed_seconds;
    payload[1] = (uint8_t)(data->elapsed_seconds >> 8);
    payload[2] = (uint8_t)data->preset_seconds;
    payload[3] = (uint8_t)(data->preset_seconds >> 8);
    payload[4] = gpio_flags;
    payload[5] = (uint8_t)state_flags;
    payload[6] = (uint8_t)(state_flags >> 8);

    uart_write_char(0xAAu);
    uart_write_char(0x55u);
    for (i = 0u; i < sizeof(payload); i++)
    {
        uart_write_char(payload[i]);
        checksum = (uint8_t)(checksum + payload[i]);
    }
    uart_write_char(checksum);
}

void uart_send_message(const char *msg, uint16_t elapsed_value, uint16_t preset_value)
{
    static uint16_t previous_elapsed;
    static uint16_t previous_preset;
    static const char *previous_message;
    char buffer[64];
    uint16_t value;
    uint16_t numbers[2];
    size_t source_index = 0u;
    size_t output_index = 0u;
    size_t number_index = 0u;

    if (msg == NULL)
    {
        return;
    }
    if (previous_message == msg && previous_elapsed == elapsed_value && previous_preset == preset_value)
    {
        return;
    }

    value = (msg == messages[UART_COUNT_FORMAT] || msg == messages[UART_COUNT_PAUSED_FORMAT])
                ? elapsed_value
                : preset_value;
    numbers[0] = value / 60u;
    numbers[1] = value % 60u;

    while (msg[source_index] != '\0' && output_index < sizeof(buffer) - 3u)
    {
        if (msg[source_index] == '%' && number_index < 2u)
        {
            uint16_t number = numbers[number_index++];
            if (number >= 10u)
            {
                buffer[output_index++] = (char)('0' + (number / 10u));
            }
            buffer[output_index++] = (char)('0' + (number % 10u));
        }
        else
        {
            buffer[output_index++] = msg[source_index];
        }
        source_index++;
    }

    buffer[output_index] = '\0';
    uart_write_string(buffer);
    previous_elapsed = elapsed_value;
    previous_preset = preset_value;
    previous_message = msg;
}
