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
#define DEBOUNCE_DELAY 95
#define MAX_PRESET_VALUE 3599
#define MIN_PRESET_VALUE 0
#define REPEAT_DELAY_MS 200
#define CLEAR_DELAY_MS 3000
#define COUNT_DELAY_MS 1000

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
static const uint16_t output[] = {13, 14, 15, 16};
static const uint16_t state_output[] = {15, 16};
static char err_msg[128];

static debounce_s push_button[3] = {
    {.pin = 11, .debounce_delay = DEBOUNCE_DELAY},
    {.pin = 9, .debounce_delay = DEBOUNCE_DELAY},
    {.pin = 10, .debounce_delay = DEBOUNCE_DELAY},
};

static led_s led[2] = {
    {.led_pin = 13, .mode = LED_BLINK, .blink_delay = 1000},
    {.led_pin = 14, .mode = LED_BLINK, .blink_delay = 250},
};

volatile timer_state_e state = TIMER_IDLE;
static int16_t preset_value = 0;
volatile uint16_t elapsed_value = 0;

static uint16_t read_control_register(void)
{
    return (push_button[0].state_bits.pb_current_state) |
           (push_button[1].state_bits.pb_current_state << 1) |
           (push_button[2].state_bits.pb_current_state << 2);
}

static void write_state_outputs(timer_state_e current_state)
{
    uint16_t state_bits = 0;

    switch (current_state)
    {
    case TIMER_IDLE:
        state_bits = 0b00;
        break;

    case TIMER_RUN:
        state_bits = 0b01;
        break;

    case TIMER_PAUSE:
        state_bits = 0b10;
        break;

    case TIMER_ALARM:
        state_bits = 0b11;
        break;

    default:
        state_bits = 0b00;
        break;
    }

    gpio_digital_write(state_output[0], state_bits & 0x1);
    gpio_digital_write(state_output[1], (state_bits >> 1) & 0x1);
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

static uint16_t delay_elapsed(uint16_t *start_time, uint16_t requested_delay)
{
    if ((uint16_t)(timer_millis() - *start_time) >= requested_delay)
    {
        *start_time = timer_millis();
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
        if (gpio_pin_mode(input[i], GPIO_INPUT_PULLDOWN) == ERROR_NONE)
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
        if (gpio_pin_mode(output[i], GPIO_OUTPUT) == ERROR_NONE)
        {
            ERROR_PRINT("Output Pin %u Setup Complete\n\r", output[i]);
        }
        else
        {
            ERROR_PRINT("Output Pin %u Setup Failed\n\r", output[i]);
        }
    }

    write_state_outputs(state);

    return ERROR_NONE;
}

error_e superloop_loop(void)
{
    uint16_t counter = 0;
    uint16_t prev_buttons = 0;
    uint16_t start_time = timer_millis();
    uint16_t trigger_flag = 0;
    timer_state_e request_state = TIMER_IDLE;
    timer_state_e prev_state = TIMER_IDLE;

    while (1)
    {
        const char *selected_message = NULL;
        uint16_t buttons = 0;

        for (uint16_t i = 0; i < ARRAY_COUNT(push_button); i++)
        {
            debounce_read(&push_button[i]);
        }
        buttons = read_control_register();

        if (buttons != prev_buttons || state != prev_state)
        {
            start_time = timer_millis();
            counter = 0;
        }

        if (request_state != state)
        {
            state = request_state;
            write_state_outputs(state);
            start_time = timer_millis();
            counter = 0;
            trigger_flag = 0;
            led[0].mode = LED_BLINK;
            led[0].st_bits.enabled = 0;
            led[0].st_bits.paused = 0;
            led[0].st_bits.output = 0;
            led[1].mode = LED_BLINK;
            led[1].st_bits.enabled = 0;
            led[1].st_bits.paused = 0;
            led[1].st_bits.output = 0;
            push_button[0].state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
            push_button[1].state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
            push_button[2].state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
        }

        preset_value = clamp_preset_value(preset_value);

        switch (state)
        {
        case TIMER_IDLE:
            if ((debounce_edge_read(&push_button[0], DEBOUNCE_FALLING_EDGE) || debounce_edge_read(&push_button[1], DEBOUNCE_FALLING_EDGE)) && trigger_flag == 1 && preset_value > 0)
            {
                request_state = TIMER_RUN;
                elapsed_value = preset_value;
                selected_message = messages[UART_COUNT_FORMAT];
            }
            else
            {
                switch (buttons)
                {
                case 0b001:
                    if (delay_elapsed(&start_time, REPEAT_DELAY_MS))
                    {
                        counter++;
                        preset_value += counter > 20 ? 5 : 1;
                        selected_message = messages[UART_SET_FORMAT];
                    }
                    break;

                case 0b010:
                    if (delay_elapsed(&start_time, REPEAT_DELAY_MS))
                    {
                        counter++;
                        preset_value += counter > 20 ? 300 : 60;
                        selected_message = messages[UART_SET_FORMAT];
                    }
                    break;

                case 0b101:
                    if (delay_elapsed(&start_time, REPEAT_DELAY_MS))
                    {
                        counter++;
                        preset_value -= counter > 20 ? 5 : 1;
                        selected_message = messages[UART_SET_FORMAT];
                    }
                    break;

                case 0b110:
                    if (delay_elapsed(&start_time, REPEAT_DELAY_MS))
                    {
                        counter++;
                        preset_value -= counter > 20 ? 300 : 60;
                        selected_message = messages[UART_SET_FORMAT];
                    }
                    break;

                case 0b011:
                    trigger_flag = 1;
                    if ((uint16_t)(timer_millis() - start_time) >= CLEAR_DELAY_MS)
                    {
                        preset_value = 0;
                        trigger_flag = 0;
                        selected_message = messages[UART_CLEAR_FORMAT];
                    }
                    break;

                case 0b111:
                    selected_message = messages[UART_INFO_FORMAT];
                    break;

                default:
                    break;
                }
            }
            break;

        case TIMER_RUN:
            selected_message = messages[UART_COUNT_FORMAT];
            led[0].mode = LED_BLINK;
            led[0].st_bits.enabled = 1;
            led[0].st_bits.paused = 0;

            if (elapsed_value == 0)
            {
                request_state = TIMER_ALARM;
                selected_message = messages[UART_ALARM_FORMAT];
            }
            else if (debounce_edge_read(&push_button[2], DEBOUNCE_RISING_EDGE))
            {
                request_state = TIMER_PAUSE;
                selected_message = messages[UART_COUNT_PAUSED_FORMAT];
                counter = 0;
            }
            break;

        case TIMER_PAUSE:
            led[0].st_bits.paused = 1;
            selected_message = messages[UART_COUNT_PAUSED_FORMAT];

            if (trigger_flag == 1 && (buttons & 0x4) == 0)
            {
                request_state = TIMER_RUN;
                selected_message = messages[UART_COUNT_FORMAT];
            }

            if (debounce_edge_read(&push_button[2], DEBOUNCE_RISING_EDGE))
            {
                trigger_flag = 1;
            }

            if (!(buttons & 0x4))
            {
                start_time = timer_millis();
            }

            if ((uint16_t)(timer_millis() - start_time) >= CLEAR_DELAY_MS)
            {
                request_state = TIMER_IDLE;
                elapsed_value = 0;
                selected_message = messages[UART_CLEAR_FORMAT];
            }
            break;

        case TIMER_ALARM:
            led[0].mode = LED_DIGITAL;
            led[0].st_bits.enabled = 1;
            led[0].st_bits.output = 1;
            led[1].mode = LED_BLINK;
            led[1].st_bits.enabled = 1;
            selected_message = messages[UART_ALARM_FORMAT];

            if ((buttons & 0x3) > 0)
            {
                request_state = TIMER_IDLE;
                selected_message = messages[UART_SET_FORMAT];
            }
            break;

        default:
            request_state = TIMER_IDLE;
            break;
        }

        for (uint16_t i = 0; i < ARRAY_COUNT(led); i++)
        {
            led_update(&led[i]);
        }

        preset_value = clamp_preset_value(preset_value);

        if (selected_message != NULL)
        {
            uart_send_message(selected_message, (uint16_t)(elapsed_value), (uint16_t)preset_value);
        }

        prev_state = state;
        prev_buttons = buttons;
    }
}
