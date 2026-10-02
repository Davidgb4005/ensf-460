#include <stdint.h>
#include "ringBuffer.h"
#include "uart.h"
#include "stdlib.h"
#include "stdio.h"
#define DEBUG 0
uint16_t dataAvailable(ring_buffer *ring_buffer)
{
    return ring_buffer->data_available;
}

ring_buffer_error getError(ring_buffer *ring_buffer)
{
    return ring_buffer->error;
}



void enqueueChar(ring_buffer *ring_buffer, uint8_t data)
{

    if (ring_buffer->data_available >= ring_buffer->size)
    {
        ring_buffer->error = BUFFER_OVERWRITE;
        #if DEBUG == 3
        char buffer[20];
        uart_write_string("Buffer Overwrite");
        #endif
        return;
    }
    if (data == '\n' || data == '\r' || data == '\0'){
        ring_buffer->string_available++;
        *(ring_buffer->write_ptr) = '\0';

    }
    else{
        *(ring_buffer->write_ptr) = data;
    }

    ring_buffer->data_available++;
    ring_buffer->write_ptr++;

    if (ring_buffer->write_ptr >= ring_buffer->buffer + ring_buffer->size)
    {
        ring_buffer->write_ptr = ring_buffer->buffer;
    }
}

void enqueueString(ring_buffer *ring_buffer, uint8_t *data)
{
    do
    {
        if (ring_buffer->data_available >= ring_buffer->size)
        {
            ring_buffer->error = BUFFER_OVERWRITE;
            #if DEBUG == 3
            char buffer[20];
            uart_write_string("Buffer Overwrite");
            #endif
            return;
        }

        enqueueChar(ring_buffer, *data);
    }
    while (*data++);

}

uint8_t dequeueChar(ring_buffer *ring_buffer)
{

    if (!ring_buffer->data_available)
    {
        ring_buffer->error = BUFFER_EMPTY;
        #if DEBUG == 3
        uart_write_string("Buffer Empty");
        #endif
        return 0;
    }

    uint8_t data = *(ring_buffer->read_ptr);
    ring_buffer->data_available--;
    ring_buffer->read_ptr++;

    if (ring_buffer->read_ptr >= ring_buffer->buffer + ring_buffer->size)
    {
        ring_buffer->read_ptr = ring_buffer->buffer;
    }

    return data;
}

void dequeueString(ring_buffer *ring_buffer, uint8_t *data)
{
    if (!ring_buffer->string_available)
    {
        ring_buffer->error = STRING_BUFFER_EMPTY;
        #if DEBUG == 3
        uart_write_string("STRING BUFFER EMPTY");
        #endif
        return;
    }

    uint8_t c;

    do
    {
        c = dequeueChar(ring_buffer);
        *data++ = c;
    }
    while (c);

    ring_buffer->string_available--;
}
