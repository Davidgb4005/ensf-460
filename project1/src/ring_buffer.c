#include <stdint.h>
#include "ring_buffer.h"
#include "uart.h"
#include "stdlib.h"
#include "stdio.h"
#define DEBUG 0
uint16_t ring_buffer_data_available(ring_buffer_s *ring_buffer)
{
    return ring_buffer->data_available;
}

error_e ring_buffer_get_error(ring_buffer_s *ring_buffer)
{
    return ring_buffer->error;
}



error_e ring_buffer_enqueue_char(ring_buffer_s *ring_buffer, uint8_t data)
{

    if (ring_buffer->data_available >= ring_buffer->size)
    {
        ring_buffer->error = ERROR_BUFFER_OVERWRITE;
        #if DEBUG == 3
        char buffer[20];
        uart_write_string("Buffer Overwrite");
        #endif
        return ERROR_BUFFER_OVERWRITE;
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
    return ERROR_NONE;
}

error_e ring_buffer_enqueue_string(ring_buffer_s *ring_buffer, uint8_t *data)
{
    do
    {
        if (ring_buffer->data_available >= ring_buffer->size)
        {
            ring_buffer->error = ERROR_BUFFER_OVERWRITE;
            #if DEBUG == 3
            char buffer[20];
            uart_write_string("Buffer Overwrite");
            #endif
            return ERROR_BUFFER_OVERWRITE;
        }

        ring_buffer_enqueue_char(ring_buffer, *data);
    }
    while (*data++);

    return ERROR_NONE;
}

uint8_t ring_buffer_dequeue_char(ring_buffer_s *ring_buffer)
{

    if (!ring_buffer->data_available)
    {
        ring_buffer->error = ERROR_BUFFER_EMPTY;
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

error_e ring_buffer_dequeue_string(ring_buffer_s *ring_buffer, uint8_t *data)
{
    if (!ring_buffer->string_available)
    {
        ring_buffer->error = ERROR_STRING_BUFFER_EMPTY;
        #if DEBUG == 3
        uart_write_string("STRING BUFFER EMPTY");
        #endif
        return ERROR_STRING_BUFFER_EMPTY;
    }

    uint8_t c;

    do
    {
        c = ring_buffer_dequeue_char(ring_buffer);
        *data++ = c;
    }
    while (c);

    ring_buffer->string_available--;
    return ERROR_NONE;
}
