#include <xc.h>
#include <p24F16KA101.h>

#include <stdint.h>
#include <stdio.h>
#include "xc.h"
#include "debounce.h"
#include "gpio.h"
#include "led.h"
#include "timer.h"
#include "uart.h"

#define ERROR_PRINT(fmt, ...)                                                         \
    do                                                                                \
    {                                                                                 \
        snprintf(err_msg, sizeof(err_msg), " %s,Line %d: " fmt,__FILE__, __LINE__, ##__VA_ARGS__); \
        uart_write_string(err_msg);                                                   \
    } while (0)

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define BUFFER_SIZE 32
#define DEBOUNCE_DELAY 0
#define MAX_PRESET_VALUE 3599
#define MIN_PRESET_VALUE 0
#define REPEAT_DELAY_MS 400
#define CLEAR_DELAY_MS 3000
#define COUNT_DELAY_MS 1000
#define STATE_SEND_DELAY_MS 125

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

static const uint16_t input[] = {11, 9, 10, 15, 16};
static const uint16_t output[] = {13, 14};
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

volatile timer_state_e state = TIMER_INIT;
volatile uint16_t elapsed_value;

static timer_state_e request_state = TIMER_INIT;
static int16_t preset_value;
static uint16_t counter;
static uint16_t prev_buttons;
static uint16_t start_time;
static uint16_t delay_ms = REPEAT_DELAY_MS;
static uint16_t state_send_time;
static timer_state_e prev_state = TIMER_IDLE;
static test_rig_data_s test_rig_data;

static uint16_t read_control_register(void)
{
    return push_button[0].state_bits.pb_current_state |
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

static uint16_t delay_elapsed(uint16_t now)
{
    if ((uint16_t)(now - start_time) >= delay_ms)
    {
        start_time = now;
        return 1;
    }
    return 0;
}

static void led_off(led_s *target)
{
    target->mode = LED_DIGITAL;
    target->st_bits.enabled = 0;
    target->st_bits.paused = 0;
    target->st_bits.output = 0;
}

static void led_on(led_s *target)
{
    target->mode = LED_DIGITAL;
    target->st_bits.enabled = 1;
    target->st_bits.paused = 0;
    target->st_bits.output = 1;
}

static void led_blink(led_s *target)
{
    target->mode = LED_BLINK;
    target->st_bits.enabled = 1;
    target->st_bits.paused = 0;
    target->st_bits.output = 0;
    target->prev_time = timer_millis();
}

static void led_pause(led_s *target)
{
    target->mode = LED_BLINK;
    target->st_bits.enabled = 1;
    target->st_bits.paused = 1;
}

static test_led_state_e led_state(const led_s *target)
{
    if (!target->st_bits.enabled)
    {
        return TEST_LED_OFF;
    }
    if (target->st_bits.paused)
    {
        return TEST_LED_PAUSE;
    }
    if (target->mode == LED_BLINK)
    {
        return TEST_LED_BLINK;
    }
    return target->st_bits.output ? TEST_LED_ON : TEST_LED_OFF;
}

error_e superloop_setup(void)
{
    AD1PCFG = 0xffff; // Disable AN11; enable RB13 digital input

    ERROR_PRINT("Output Pin %u Setup Failed", output[i]);
    if (uart_init(&rx_buffer, &tx_buffer) == 0)
    {
        ERROR_PRINT("%s", "UART Setup Complete");
    }

    if (timer2_init() == 0)
    {
        ERROR_PRINT("%s", "Timer 2 Setup Complete");
    }
    else
    {
        ERROR_PRINT("%s", "Timer 2 Setup Failed");
    }

    for (uint16_t i = 0; i < ARRAY_COUNT(input); i++)
    {
        if (gpio_pin_mode(input[i], GPIO_INPUT_PULLDOWN) == ERROR_NONE)
        {
            ERROR_PRINT("Input Pin %u Setup Complete", input[i]);
        }
        else
        {
            ERROR_PRINT("Input Pin %u Setup Failed", input[i]);
        }
    }

    for (uint16_t i = 0; i < ARRAY_COUNT(output); i++)
    {
        if (gpio_pin_mode(output[i], GPIO_OUTPUT) == ERROR_NONE)
        {
            ERROR_PRINT("Output Pin %u Setup Complete", output[i]);
        }
        else
        {
            ERROR_PRINT("Output Pin %u Setup Failed", output[i]);
        }
    }

    led_off(&led[0]);
    led_off(&led[1]);
    start_time = timer_millis();
    state_send_time = start_time;
    return ERROR_NONE;
}

error_e superloop_loop(void)
{

    if (gpio_digital_read(16))
    {
        __asm__ volatile("reset");
    }

    const char *selected_message = NULL;
    uint16_t buttons;
    uint16_t now;
    uint16_t state_changed = 0;

    for (uint16_t i = 0; i < ARRAY_COUNT(push_button); i++)
    {
        debounce_read(&push_button[i]);
    }

    now = timer_millis();
    buttons = read_control_register();

    if (buttons != prev_buttons)
    {
        prev_buttons = buttons;
        counter = 0;
        start_time = now;
    }

    if (request_state != state)
    {
        prev_state = state;
        state = request_state;
        state_changed = 1;

        start_time = now;
        counter = 0;
        push_button[0].state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
        push_button[1].state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
        push_button[2].state_bits.most_recent_edge = DEBOUNCE_NO_EDGE;
    }

    switch (state)
    {
    case TIMER_INIT_ENTRY:
        led_off(&led[0]);
        led_off(&led[1]);
        request_state = TIMER_INIT;
        break;
    case TIMER_INIT:
        request_state = TIMER_INIT_TRANSITION;
        break;
    case TIMER_INIT_TRANSITION:
        request_state = TIMER_INIT_TO_RESET;
        break;
    case TIMER_INIT_TO_RESET:
        request_state = TIMER_RESET;
        break;

    case TIMER_IDLE_ENTRY:
        led_off(&led[0]);
        led_off(&led[1]);
        delay_ms = REPEAT_DELAY_MS;
        request_state = TIMER_IDLE;
        selected_message = messages[UART_SET_FORMAT];
        break;

    case TIMER_IDLE:
        switch (buttons)
        {
        case 0x1:
            if (delay_elapsed(now))
            {
                counter++;
                preset_value += counter > 20 ? 5 : 1;
                selected_message = messages[UART_SET_FORMAT];
            }
            break;
        case 0x2:
            if (delay_elapsed(now))
            {
                counter++;
                preset_value += counter > 20 ? 300 : 60;
                selected_message = messages[UART_SET_FORMAT];
            }
            break;
        case 0x5:
            if (delay_elapsed(now))
            {
                counter++;
                preset_value -= counter > 20 ? 5 : 1;
                selected_message = messages[UART_SET_FORMAT];
            }
            break;
        case 0x6:
            if (delay_elapsed(now))
            {
                counter++;
                preset_value -= counter > 20 ? 300 : 60;
                selected_message = messages[UART_SET_FORMAT];
            }
            break;
        case 0x3:
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_IDLE_TRANSITION;
            break;
        case 0x7:
            selected_message = messages[UART_INFO_FORMAT];
            break;
        default:
            break;
        }
        break;

    case TIMER_IDLE_TRANSITION:
        if (buttons == 0x3)
        {
            if (delay_elapsed(now))
            {
                request_state = TIMER_IDLE_TO_RESET;
            }
        }
        else if (preset_value > 0)
        {
            request_state = TIMER_IDLE_TO_RUN;
        }
        else
        {
            request_state = TIMER_IDLE_ENTRY;
        }
        break;
    case TIMER_IDLE_TO_RUN:
        elapsed_value = (uint16_t)preset_value;
        request_state = TIMER_RUN_ENTRY;
        break;
    case TIMER_IDLE_TO_RESET:
        request_state = TIMER_RESET_ENTRY;
        break;

    case TIMER_RESET_ENTRY:
        led_off(&led[0]);
        led_off(&led[1]);
        request_state = TIMER_RESET;
        break;
    case TIMER_RESET:
        preset_value = 0;
        request_state = TIMER_RESET_TRANSITION;
        selected_message = messages[UART_CLEAR_FORMAT];
        break;

    case TIMER_RESET_TRANSITION:
    case TIMER_RESET_TO_IDLE:
        request_state = TIMER_IDLE_ENTRY;
        break;

    case TIMER_RUN_ENTRY:
        led_blink(&led[0]);
        led_off(&led[1]);
        delay_ms = COUNT_DELAY_MS;
        request_state = TIMER_RUN;
        selected_message = messages[UART_COUNT_FORMAT];
        break;
    case TIMER_RUN:
        if (elapsed_value == 0 || (buttons & 0x4) != 0)
        {
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_RUN_TRANSITION;
        }
        if (delay_elapsed(now))
        {
            if (elapsed_value > 0)
            {
                elapsed_value--;
            }
            selected_message = messages[UART_COUNT_FORMAT];
        }
        break;

    case TIMER_RUN_TRANSITION:
        if (elapsed_value == 0)
        {
            request_state = TIMER_RUN_TO_ALARM;
        }
        else if ((buttons & 0x4) != 0)
        {
            if (delay_elapsed(now))
            {
                request_state = TIMER_RUN_TO_CLEAR;
            }
        }
        else
        {
            request_state = TIMER_RUN_TO_PAUSE;
        }
        break;

    case TIMER_RUN_TO_ALARM:
        request_state = TIMER_ALARM_ENTRY;
        break;
    case TIMER_RUN_TO_PAUSE:
        request_state = TIMER_PAUSE_ENTRY;
        break;
    case TIMER_RUN_TO_CLEAR:
        request_state = TIMER_CLEAR_ENTRY;
        break;

    case TIMER_PAUSE_ENTRY:
        led_pause(&led[0]);
        led_off(&led[1]);
        delay_ms = CLEAR_DELAY_MS;
        request_state = TIMER_PAUSE;
        selected_message = messages[UART_COUNT_PAUSED_FORMAT];
        break;

    case TIMER_PAUSE:
        if ((buttons & 0x4) != 0)
        {
            request_state = TIMER_PAUSE_TRANSITION;
        }
        break;

    case TIMER_PAUSE_TRANSITION:
        if ((buttons & 0x4) != 0)
        {
            if (delay_elapsed(now))
            {
                request_state = TIMER_PAUSE_TO_CLEAR;
            }
        }
        else
        {
            request_state = TIMER_PAUSE_TO_RUN;
        }
        break;
    case TIMER_PAUSE_TO_RUN:
        request_state = TIMER_RUN_ENTRY;
        break;
    case TIMER_PAUSE_TO_CLEAR:
        request_state = TIMER_CLEAR_ENTRY;
        break;

    case TIMER_CLEAR_ENTRY:
        led_off(&led[0]);
        led_off(&led[1]);
        request_state = TIMER_CLEAR;
        break;
    case TIMER_CLEAR:
        elapsed_value = 0;
        request_state = TIMER_CLEAR_TRANSITION;
        selected_message = messages[UART_CLEAR_FORMAT];
        break;
    case TIMER_CLEAR_TRANSITION:
        request_state = TIMER_CLEAR_TO_IDLE;
        break;
    case TIMER_CLEAR_TO_IDLE:
        request_state = TIMER_IDLE_ENTRY;
        break;

    case TIMER_ALARM_ENTRY:
        led_on(&led[0]);
        led_blink(&led[1]);
        request_state = TIMER_ALARM;
        selected_message = messages[UART_ALARM_FORMAT];
        break;
    case TIMER_ALARM:
        if ((buttons & 0x3) != 0)
        {
            request_state = TIMER_ALARM_TRANSITION;
        }
        break;
    case TIMER_ALARM_TRANSITION:
        led_off(&led[0]);
        led_off(&led[1]);
        request_state = TIMER_ALARM_TO_IDLE;
        break;
    case TIMER_ALARM_TO_IDLE:
        request_state = TIMER_IDLE_ENTRY;
        break;

    default:
        request_state = TIMER_INIT_ENTRY;
        break;
    }

    preset_value = clamp_preset_value(preset_value);

    for (uint16_t i = 0; i < ARRAY_COUNT(led); i++)
    {
        led_update(&led[i]);
    }

    if (state_changed || (uint16_t)(now - state_send_time) >= STATE_SEND_DELAY_MS)
    {
        state_send_time = now;
        test_rig_data.elapsed_seconds = elapsed_value;
        test_rig_data.preset_seconds = (uint16_t)preset_value;
        test_rig_data.states.state_code = (uint16_t)state;
        test_rig_data.states.prev_state_code = (uint16_t)prev_state;
        test_rig_data.gpio.led_1_state = led_state(&led[0]);
        test_rig_data.gpio.led_2_state = led_state(&led[1]);
        test_rig_data.gpio.pb_1 = push_button[0].state_bits.pb_current_state;
        test_rig_data.gpio.pb_2 = push_button[1].state_bits.pb_current_state;
        test_rig_data.gpio.pb_3 = push_button[2].state_bits.pb_current_state;
        uart_send_state(&test_rig_data);
    }

    if (selected_message != NULL)
    {
        uart_send_message(selected_message, elapsed_value, (uint16_t)preset_value);
    }

    return ERROR_NONE;
}
