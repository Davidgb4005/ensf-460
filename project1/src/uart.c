#include <xc.h>
#include <stdint.h>
#include "uart.h"
#include "stdlib.h"
#include "stdio.h"

#define FCY         4000000UL
#define BAUD_RATE   57600UL
#define ECHO_ENABLE 1

ring_buffer_s *rx_buffer;
ring_buffer_s *tx_buffer;


const char *messages[] = {
    LINE_CLEAR("CNT %m : %s"),
    LINE_CLEAR("CNT %m : %s {PAUSED}"),
    LINE_CLEAR("FIN 00m : 00s - ALARM"),
    LINE_CLEAR("SET %m : %s"),
    LINE_CLEAR("2026 ENSF 460 L01 - Group 06"),
    LINE_CLEAR("CLR 00m : 00s")
};

uint16_t uart_init(ring_buffer_s *uart_rx_buffer, ring_buffer_s *uart_tx_buffer)
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

error_e uart_int_to_ascii(uint16_t data, uint8_t *buffer)
{
    sprintf((char *)buffer, "%u", data);
    return ERROR_NONE;
}

error_e uart_write_char(uint8_t data)
{
    while (U2STAbits.UTXBF);
    U2TXREG = data;
    return ERROR_NONE;
}

error_e uart_write_string(const char *data)
{
    while (*data)
    {
        uart_write_char((uint8_t)*data++);
    }
    uart_write_char('\r');
    uart_write_char('\n');
    return ERROR_NONE;
}

uint8_t uart_read_char(void)
{
    while (!ring_buffer_data_available(rx_buffer));
    return ring_buffer_dequeue_char(rx_buffer);
}

error_e uart_send_buffer(void)
{
    while (ring_buffer_data_available(tx_buffer))
    {
        uart_write_char(ring_buffer_dequeue_char(tx_buffer));
    }
    return ERROR_NONE;
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
        ring_buffer_enqueue_char(rx_buffer, c);
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

error_e uart_send_state(const test_rig_data_s *data)
  {
      uint16_t flags =
            ((data->bits.state_code     & 0x07u) << 0)
          | ((data->bits.prev_state_code & 0x07u) << 3)
          | ((data->bits.pb_1            & 0x01u) << 6)
          | ((data->bits.pb_2            & 0x01u) << 7)
          | ((data->bits.pb_3            & 0x01u) << 8)
          | ((data->bits.led_1_state     & 0x03u) << 9)
          | ((data->bits.led_2_state     & 0x03u) << 11);

      uint8_t payload[6] = {
          (uint8_t)(data->elapsed_seconds),
          (uint8_t)(data->elapsed_seconds >> 8),
          (uint8_t)(data->preset_seconds),
          (uint8_t)(data->preset_seconds >> 8),
          (uint8_t)(flags),
          (uint8_t)(flags >> 8),
      };

      uint8_t checksum = 0;

      uart_write_char((char)0xAA);
      uart_write_char((char)0x55);

      for (size_t i = 0; i < sizeof(payload); i++) {
          uart_write_char((char)payload[i]);
          checksum = (uint8_t)(checksum + payload[i]);
      }

      uart_write_char((char)checksum);

      return ERROR_NONE; // Replace with your success value
}


error_e uart_send_message(const char *msg, uint16_t elapsed_value,uint16_t preset_value)
{
    static uint16_t prev_elapsed;
    static uint16_t prev_preset;
    static const char *prev_cursor;
    if (msg == NULL)
        return ERROR_NULL_VALUE;
    if(prev_cursor == msg && prev_elapsed == elapsed_value && prev_preset == preset_value){
        return ERROR_NONE;
    }
    const char *cursor = msg;
    char tmp_buffer[64];

    uint16_t value = (cursor == messages[UART_COUNT_FORMAT] || cursor == messages[UART_COUNT_PAUSED_FORMAT])
                         ? elapsed_value
                         : preset_value;

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
    prev_elapsed = elapsed_value;
    prev_preset = preset_value;
    prev_cursor = msg;
    return ERROR_NONE;
}
#endif
