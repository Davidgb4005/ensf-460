#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>

typedef enum {
    NO_ERROR,
    BUFFER_OVERWRITE,
    BUFFER_OVERREAD,
    BUFFER_EMPTY,
    STRING_BUFFER_EMPTY,
} ring_buffer_error;

typedef struct
{
    volatile uint8_t *buffer;
    volatile uint8_t *read_ptr;
    volatile uint8_t *write_ptr;
    volatile uint16_t data_available;
    volatile uint16_t string_available;
    uint16_t size;
    ring_buffer_error error;
} ring_buffer;

uint16_t dataAvailable(ring_buffer *ring_buffer);
ring_buffer_error getError(ring_buffer *ring_buffer);
void enqueueChar(ring_buffer *ring_buffer, uint8_t data);
void enqueueString(ring_buffer *ring_buffer, uint8_t *data);
uint8_t dequeueChar(ring_buffer *ring_buffer);
void dequeueString(ring_buffer *ring_buffer, uint8_t *data);

#endif
