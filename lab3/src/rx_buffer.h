#ifndef _PICSDK_RX_BUFFER_H
#define _PICSDK_RX_BUFFER_H

#include <stdint.h>
#include <errno.h>
#include <stdbool.h>

#define RX_BUFFER_SIZE 128

typedef volatile struct rx_buffer_s {
  uint8_t data[RX_BUFFER_SIZE];
  uint16_t tail_idx;
  uint16_t head_idx;
} rx_buffer_s;

static inline bool rx_buffer_is_full(rx_buffer_s *rx_buf) {
   return (rx_buf->head_idx + 1) % RX_BUFFER_SIZE == rx_buf->tail_idx;
}

static inline bool rx_buffer_is_empty(rx_buffer_s *rx_buf) {
  return rx_buf->tail_idx == rx_buf->head_idx;
}

static inline int rx_buffer_get(rx_buffer_s *rx_buf, uint8_t *rx) {
  // Return if buffer is empty
  if (rx_buffer_is_empty(rx_buf)) {
    return -EAGAIN;
  }

  *rx = rx_buf->data[rx_buf->tail_idx];
  rx_buf->tail_idx = (rx_buf->tail_idx + 1) % RX_BUFFER_SIZE;

  return 0;
}

static inline void rx_buffer_put(rx_buffer_s *rx_buf, uint8_t data) {
  // Do not overwrite buffer if it is full
  if (rx_buffer_is_full(rx_buf)) {
    return;
  }

  // Write to the buffer
  rx_buf->data[rx_buf->head_idx] = data;
  rx_buf->head_idx = (rx_buf->head_idx + 1) % RX_BUFFER_SIZE;
}

#endif
