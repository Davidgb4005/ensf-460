#include <xc.h>
#include <p24F16KA101.h>

#include <stdint.h>
#include <stdio.h>

#include "timer.h"
#include "uart.h"
#include "gpio.h"
#include "debounce.h"
#include "led.h"

#define ERROR_PRINT(fmt, ...)                                                         \
    do                                                                                \
    {                                                                                 \
        snprintf(err_msg, sizeof(err_msg), "Line %d: " fmt, __LINE__, ##__VA_ARGS__); \
        uart_write_string(err_msg);                                                   \
    } while (0)

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define BUFFER_SIZE 32
#define DEBOUNCE_DELAY 150
#define MAX_PRESET_VALUE 5999
#define MIN_PRESET_VALUE 0
#define REPEAT_DELAY_MS 200
#define CLEAR_DELAY_MS 3000
#define COUNT_DELAY_MS 1000

typedef enum
{
    TIMER_IDLE,
    TIMER_RUN,
    TIMER_PAUSE,
    TIMER_ALARM,
} timer_state_e;

static uint8_t rx_data[BUFFER_SIZE];
static uint8_t tx_data[BUFFER_SIZE];

static ring_buffer_s rx_buffer = {
    .buffer = rx_data,
    .read_ptr = rx_data,
    .write_ptr = rx_data,
    .size = BUFFER_SIZE,
    .data_available = 0,
    .string_available = 0,
    .error = ERROR_NONE,
};

static ring_buffer_s tx_buffer = {
    .buffer = tx_data,
    .read_ptr = tx_data,
    .write_ptr = tx_data,
    .size = BUFFER_SIZE,
    .data_available = 0,
    .string_available = 0,
    .error = ERROR_NONE,
};

static const uint16_t input[] = {11, 9, 10};
static const uint16_t output[] = {13, 14};
static char err_msg[128];

static debounce_s push_button[3] = {
    {.pin = 11, .debounce_delay = DEBOUNCE_DELAY},
    {.pin = 9, .debounce_delay = DEBOUNCE_DELAY},
    {.pin = 10, .debounce_delay = DEBOUNCE_DELAY},
};

static led_s led[2] = {
    {.led_pin = 13, .mode = LED_BLINK},
    {.led_pin = 14, .mode = LED_BLINK},
};

static timer_state_e state = TIMER_IDLE;
static timer_state_e prev_state = TIMER_IDLE;
static uint16_t control_register = 0;
static uint16_t prev_control_register = 0;
static uint16_t prev_delay = 0;
static uint16_t delay = 0;
static uint16_t counter = 0;
static uint16_t action_timed_out = 0;
static int16_t preset_value = 0;
static int16_t elapsed_value = 0;

static uint16_t read_control_register(void)
{
    return (push_button[0].state_bits.pb_current_state) |
           (push_button[1].state_bits.pb_current_state << 1) |
           (push_button[2].state_bits.pb_current_state << 2);
}

static int16_t clamp_preset_value(int16_t value)
{
    if (value > MAX_PRESET_VALUE)
    {
        return MAX_PRESET_VALUE;
    }

    if (value < MIN_PRESET_VALUE)
    {
        return MIN_PRESET_VALUE;
    }

    return value;
}

static uint16_t delay_elapsed(uint16_t requested_delay)
{
    if ((uint16_t)(timer_millis() - prev_delay) > requested_delay)
    {
        prev_delay = timer_millis();
        return 1;
    }

    return 0;
}

error_e superloop_setup(void)
{
    if (uart_init(&rx_buffer, &tx_buffer) == 0)
    {
        ERROR_PRINT("UART Setup Complete\n\r");
    }

    if (timer2_init() == 0)
    {
        ERROR_PRINT("Timer 2 Setup Complete\n\r");
    }
    else
    {
        ERROR_PRINT("Timer 2 Setup Failed\n\r");
    }

    for (uint16_t i = 0; i < ARRAY_COUNT(input); i++)
    {
        if (gpio_pin_mode(input[i], GPIO_INPUT_PULLDOWN) != 0)
        {
            ERROR_PRINT("Input Pin %u Setup Complete\n\r", input[i]);
        }
        else
        {
            ERROR_PRINT("Input Pin %u Setup Failed\n\r", input[i]);
        }
    }

    for (uint16_t i = 0; i < ARRAY_COUNT(output); i++)
    {
        if (gpio_pin_mode(output[i], GPIO_OUTPUT) != 0)
        {
            ERROR_PRINT("Output Pin %u Setup Complete\n\r", output[i]);
        }
        else
        {
            ERROR_PRINT("Output Pin %u Setup Failed\n\r", output[i]);
        }
    }

    return ERROR_NONE;
}

error_e superloop_loop(void)
{
    const char *selected_message = NULL;
    uint16_t request_message = 0;

    for (uint16_t i = 0; i < ARRAY_COUNT(push_button); i++)
    {
        debounce_read(&push_button[i]);
    }

    control_register = read_control_register();

    preset_value = clamp_preset_value(preset_value);
    led[0].st_bits.enabled = 0;
    led[0].st_bits.paused = 0;
    led[1].st_bits.enabled = 0;
    led[1].st_bits.paused = 0;
    counter++;

    switch (state)
    {
    case TIMER_IDLE:
        selected_message = messages[UART_SET_FORMAT];

        if (prev_control_register == 0b011 && control_register != 0b011)
        {
            if (!action_timed_out &&
                (debounce_edge_read(&push_button[0], DEBOUNCE_FALLING_EDGE) ||
                 debounce_edge_read(&push_button[1], DEBOUNCE_FALLING_EDGE)))
            {
                state = TIMER_RUN;
                elapsed_value = preset_value;
                selected_message = messages[UART_COUNT_FORMAT];
                request_message = 1;
                break;
            }
        }
        switch (control_register)
        {
        case 0b001:
            delay = REPEAT_DELAY_MS;
            if (delay_elapsed(delay))
            {
                preset_value += counter > 20 ? 1 : 5;
                request_message = 1;
            }
            break;

        case 0b010:
            delay = REPEAT_DELAY_MS;
            if (delay_elapsed(delay))
            {
                preset_value += counter > 20 ? -1 : -300;
                request_message = 1;
            }
            break;

        case 0b101:
            delay = REPEAT_DELAY_MS;
            if (delay_elapsed(delay))
            {
                preset_value += counter > 20 ? -1 : -5;
                request_message = 1;
            }
            break;

        case 0b110:
            delay = REPEAT_DELAY_MS;
            if (delay_elapsed(delay))
            {
                preset_value += counter > 20 ? -60 : -300;
                request_message = 1;
            }
            break;

        case 0b011:
            delay = CLEAR_DELAY_MS;
            if (delay_elapsed(delay))
            {
                preset_value = 0;
                action_timed_out = 1;
                request_message = 1;
                break;
            }

            break;

        case 0b111:
            selected_message = messages[UART_INFO_FORMAT];
            request_message = 1;
            break;

        default:
            break;
        }
        break;

    case TIMER_RUN:
        selected_message = messages[UART_COUNT_FORMAT];
        led[0].blink_delay = COUNT_DELAY_MS;
        led[0].st_bits.enabled = 1;
        delay = COUNT_DELAY_MS;

        if (elapsed_value == 0)
        {
            state = TIMER_ALARM;
            selected_message = messages[UART_ALARM_FORMAT];
            request_message = 1;
            break;
        }

        if (delay_elapsed(delay))
        {
            elapsed_value -= 1;
            request_message = 1;
        }

        if (prev_control_register == 0b100 && control_register != 0b100)
        {
            if (!action_timed_out &&
                debounce_edge_read(&push_button[2], DEBOUNCE_FALLING_EDGE))
            {
                state = TIMER_PAUSE;
                break;
            }
        }

        if (control_register == 0b100)
        {

            if (counter > 3)
            {
                preset_value = 0;
                action_timed_out = 1;
                selected_message = messages[UART_CLEAR_FORMAT];
                request_message = 1;
                break;
            }
        }
        break;

    case TIMER_PAUSE:
        selected_message = messages[UART_COUNT_FORMAT];
        led[0].blink_delay = COUNT_DELAY_MS;
        led[0].st_bits.enabled = 1;
        led[0].st_bits.paused = 1;

        if (prev_control_register == 0b100 && control_register != 0b100)
        {
            if (!action_timed_out &&
                debounce_edge_read(&push_button[2], DEBOUNCE_FALLING_EDGE))
            {
                state = TIMER_RUN;
                break;
            }
        }

        if (control_register == 0b100)
        {
            delay = COUNT_DELAY_MS;
            if (delay_elapsed(delay))
            {
                preset_value = 0;
                action_timed_out = 1;
                selected_message = messages[UART_CLEAR_FORMAT];
                request_message = 1;
            }
        }
        break;

    case TIMER_ALARM:
        led[1].blink_delay = COUNT_DELAY_MS;
        led[1].st_bits.enabled = 1;
        selected_message = messages[UART_ALARM_FORMAT];

        if (control_register != 0)
        {
            state = TIMER_IDLE;
            elapsed_value = 0;
            request_message = 1;
        }
        break;

    default:
        state = TIMER_IDLE;
        break;
    }

    preset_value = clamp_preset_value(preset_value);

    for (uint16_t i = 0; i < ARRAY_COUNT(led); i++)
    {
        led_update(&led[i]);
    }

    if (request_message && selected_message != NULL)
    {
        uart_send_message(selected_message, (uint16_t)elapsed_value, (uint16_t)preset_value);
    }

    if (prev_control_register != control_register || state != prev_state)
    {
        prev_delay = timer_millis();
        delay = 0;
        counter = 0;
        action_timed_out = 0;
    }

    prev_control_register = control_register;
    prev_state = state;
    return ERROR_NONE;
}
