ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include "error.h"

typedef struct
{
    volatile uint8_t *buffer;
    volatile uint8_t *read_ptr;
    volatile uint8_t *write_ptr;
    volatile uint16_t data_available;
    volatile uint16_t string_available;
    uint16_t size;
    error_e error;
} ring_buffer_s;

uint16_t ring_buffer_data_available(ring_buffer_s *ring_buffer);
error_e ring_buffer_get_error(ring_buffer_s *ring_buffer);
error_e ring_buffer_enqueue_char(ring_buffer_s *ring_buffer, uint8_t data);
error_e ring_buffer_enqueue_string(ring_buffer_s *ring_buffer, uint8_t *data);
uint8_t ring_buffer_dequeue_char(ring_buffer_s *ring_buffer);
error_e ring_buffer_dequeue_string(ring_buffer_s *ring_buffer, uint8_t *data);

#endif
