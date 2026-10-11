
#include <xc.h>
#include <p24F16KA101.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "timer.h"
#include "uart.h"
#include "gpio.h"
#include "debounce.h"
#include "led.h"

#define ERROR_PRINT(fmt, ...)                                                   \
    do {                                                                        \
        snprintf(err_msg, sizeof(err_msg), "Line %d: " fmt, __LINE__, ##__VA_ARGS__); \
        uart_write_string(err_msg);                                             \
    } while (0)

#define BUFFER_SIZE 32
#define DEBOUNCE_DELAY 150

static uint8_t rx_data[BUFFER_SIZE];
static uint8_t tx_data[BUFFER_SIZE];

static ring_buffer rx_buffer = {
    .buffer = rx_data,
    .read_ptr = rx_data,
    .write_ptr = rx_data,
    .size = BUFFER_SIZE,
    .data_available = 0,
    .string_available = 0,
    .error = 0,
};

static ring_buffer tx_buffer = {
    .buffer = tx_data,
    .read_ptr = tx_data,
    .write_ptr = tx_data,
    .size = BUFFER_SIZE,
    .data_available = 0,
    .string_available = 0,
    .error = 0,
};

static uint16_t input[] = {11, 9, 10};
static uint16_t output[] = {13, 14};
static char err_msg[128];

static debounce push_button[3] = {
    {.pin = 11, .debounce_delay = DEBOUNCE_DELAY},
    {.pin = 9,  .debounce_delay = DEBOUNCE_DELAY},
    {.pin = 10, .debounce_delay = DEBOUNCE_DELAY}
};

static led_t led[2] = {
    {.led_pin = 13, .mode = DIGITAL},
    {.led_pin = 14, .mode = BLINK}
};

void setup(void)
{
    // UART 2 Setup
    if (uart_init(&rx_buffer, &tx_buffer) == 0)
    {
        ERROR_PRINT("UART Setup Complete\n\r");
    }

    // Timer 2 Setup
    if (timer2_init() == 0)
    {
        ERROR_PRINT("Timer 2 Setup Complete\n\r");
    }
    else
    {
        ERROR_PRINT("Timer 2 Setup Failed\n\r");
    }

    // Input Pin Setup
    for (uint16_t i = 0; i < sizeof(input) / sizeof(input[0]); i++)
    {
        if (pinMode(input[i], INPUT_PULLDOWN) == 0)
        {
            ERROR_PRINT("Input Pin %u Setup Complete\n\r", input[i]);
        }
        else
        {
            ERROR_PRINT("Input Pin %u Setup Failed\n\r", input[i]);
        }
    }

    // Output Pin Setup
    for (uint16_t i = 0; i < sizeof(output) / sizeof(output[0]); i++)
    {
        if (pinMode(output[i], OUTPUT) == 0)
        {
            ERROR_PRINT("Output Pin %u Setup Complete\n\r", output[i]);
        }
        else
        {
            ERROR_PRINT("Output Pin %u Setup Failed\n\r", output[i]);
        }
    }

    // LED Setup
    led[0].st_bits.enabled = 1;
    led[0].st_bits.output = 0;

    led[1].st_bits.enabled = 1;
    led[1].st_bits.output = 0;
    led[1].blink_delay = 500;
    led[1].prev_time = millis();
}

void loop(void)
{
    // Button Debounce
    for (uint16_t i = 0; i < sizeof(push_button) / sizeof(push_button[0]); i++)
    {
        debounce_read(&push_button[i]);
    }

    // LED 1: Always ON
    writeLed(&led[0], 1);

    // LED 2: Blink every 500 ms
    updateLed(&led[1]);
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    _CNIF = 0;
}