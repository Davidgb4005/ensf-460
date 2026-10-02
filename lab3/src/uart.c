#include "uart.h"

#include <xc.h>

#include <stdint.h>
#include <errno.h>

#include "rx_buffer.h"


static volatile rx_buffer_s u1_buf;
static volatile rx_buffer_s u2_buf;

int uart_init(uint8_t channel, uint32_t baud_rate) {
  int ret = 0;

  if (OSCCONbits.COSC == 0b110) {
    U2BRG = 12;	// gives a baud rate of 4807.7 Baud with 500kHz clock; Set Baud to 4800 on realterm
  }
  else if (OSCCONbits.COSC == 0b101) {
    U2BRG = 12;	// gives a baud rate of 300 Baud with 32kHz clock; set Baud to 300 on realterm
  }
  else if (OSCCONbits.COSC == 0b000) {
    U2BRG=103;	// gives a baud rate of 9600 with 8MHz clock; set Baud to 9600 on real term
  }

  uint32_t freq;
  switch (OSCCONbits.COSC) {
    case 0b110:         // 500 kHz
      freq = 250000;
      break;

    case 0b101:         // 32 kHz
      freq = 32767;
      break;

    default:
    case 0b000:         // 8 MHz
      freq = 4000000;
      break;
  }

  const uint16_t brg = (uint16_t)((uint32_t)freq / (16UL * baud_rate)) - 1;

  switch(channel) {
    case 1:
      // Disable channel
      U1MODEbits.UARTEN = 0;

      // Default to 8-bit data, no parity, one stop bit
      U1MODEbits.PDSEL = 0x0;
      U1MODEbits.STSEL = 0;

      // Use low baud rate mode
      U1MODEbits.BRGH = 0;
      U1BRG = brg;

      // Enable receive interrupt
      U1STAbits.URXISEL = 00;
      _U1RXIE = 1;

      // Enable channel
      U1MODEbits.UARTEN = 1;

      // Enable transmission
      U1STAbits.UTXEN = 1;
      break;

    case 2:
      // Disable channel
      U2MODEbits.UARTEN = 0;

      // Default to 8-bit data, no parity, one stop bit
      U2MODEbits.PDSEL = 0x0;
      U2MODEbits.STSEL = 0;

      // Use low baud rate mode
      U2MODEbits.BRGH = 0;
      U2BRG = brg;

      // Enable receive interrupt
      U2STAbits.URXISEL = 00;
      _U2RXIE = 1;

      // Enable channel
      U2MODEbits.UARTEN = 1;

      // Enable transmission
      U2STAbits.UTXEN = 1;
      break;

    default:
      ret = -EINVAL;
      break;
  }

  return ret;
}

int uart_write(uint8_t channel, uint8_t tx) {
  int ret = 0;

  switch (channel) {
    case 1: 
      // Wait for the transmission buffer to have a free slot
      while (U1STAbits.UTXBF) (void)0;

      U1TXREG = tx;

      // Wait until transmission is complete
      while (!U1STAbits.TRMT) (void)0;
      break;

    case 2:
      // Wait for the transmission buffer to hav a free slot
      while (U2STAbits.UTXBF) (void)0;

      U2TXREG = tx;

      // Wait until transmission is complete
      while (!U2STAbits.TRMT) (void)0;
      break;

    default:
      ret = -EINVAL;
      break;
  }

  return ret;
}

int uart_get(uint8_t channel, uint8_t *rx) {
  int ret = 0;

  switch (channel) {
    case 1:
      ret = rx_buffer_get(&u1_buf, rx);
      break;

    case 2:
      ret = rx_buffer_get(&u2_buf, rx);
      break;

    default:
      ret = -EINVAL;
  }

  return ret;
}

int uart_writen(uint8_t channel, const uint8_t *tx, size_t n) {
  int ret = 0;

  for (size_t i = 0; i < n; i++) { 
    uart_write(channel, tx[i]);
  }

  return ret;
}

void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void) {
  _U1RXIF = 0;

  // Wait for data to be available
  while (U1STAbits.URXDA) {
    rx_buffer_put(&u1_buf, U1RXREG);
  }

  // Clear overrun error
  if (U1STAbits.OERR) {
    U1STAbits.OERR = 0;
  }
}

void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void) {
  _U2RXIF = 0;

  // Wait for data to be available
  while (U2STAbits.URXDA) {
    rx_buffer_put(&u2_buf, U2RXREG);
  }

  // Clear overrun error
  if (U2STAbits.OERR) {
    U2STAbits.OERR = 0;
  }
}

static bool uart_ready = false;
int __attribute__((__section__(".libc.write")))
write(int file, char *ptr, int len) {
  (void)file;
  (void)ptr;

  if (!uart_ready) {
    uart_init(2, 4800);
    uart_ready = true;
  }

  for (int i = 0; i < len; i++) {
    if (ptr[i] == '\n') {
      uart_write(2, '\r');
    }

    uart_write(2, ptr[i]);
  }
    
  return len;
}
