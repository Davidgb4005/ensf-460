#include <xc.h>
#include <p24F16KA101.h>

#include <stdint.h>
#include <stdio.h>
#include "xc.h"
#include "timer.h"
#include "uart.h"

#define ERROR_PRINT(fmt, ...)                                                                       \
    do                                                                                              \
    {                                                                                               \
        snprintf(err_msg, sizeof(err_msg), " %s,Line %d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        uart_write_string(err_msg);                                                                 \
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
#define FAST_BLINK_MS 250;

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

static timer_state_e state = TIMER_INIT;
static volatile uint16_t elapsed_value;

static timer_state_e request_state = TIMER_INIT;
static int16_t preset_value;
static uint16_t counter;
static uint16_t prev_buttons = 0;
static uint16_t start_time;
static uint16_t delay_ms = REPEAT_DELAY_MS;
static uint16_t state_send_time;
static timer_state_e prev_state = TIMER_IDLE;
static test_rig_data_s test_rig_data;
static uint16_t send_delay_ms = 250;
static uint16_t send_start_time;

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

void sendTestRigData()
{
    state_send_time = timer_millis();
    test_rig_data.elapsed_seconds = elapsed_value;
    test_rig_data.preset_seconds = (uint16_t)preset_value;
    test_rig_data.states.state_code = (uint16_t)state;
    test_rig_data.states.prev_state_code = (uint16_t)prev_state;
    test_rig_data.gpio.led_1_state = _RB9;
    test_rig_data.gpio.led_2_state = _RA6;
    test_rig_data.gpio.pb_1 = _RB7;
    test_rig_data.gpio.pb_2 = _RB4;
    test_rig_data.gpio.pb_3 = _RA4;
    uart_send_state(&test_rig_data);
}
static uint16_t sleep_counter = 0;
void sleep_after_cycle()
{
    if (sleep_counter > 0)
    {
        sleep_counter = 0;
        Sleep();
    }
    else
    {
        sleep_counter++;
    }
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
static uint16_t send_delay_elapsed(uint16_t now)
{
    if ((uint16_t)(now - send_start_time) >= send_delay_ms)
    {
        send_start_time = now;
        return 1;
    }
    return 0;
}
uint16_t dummy;
#if 0
#define LED_1 _LATB9
#define LED_2 _LATA6
#endif
#if 1
#define LED_1 dummy
#define LED_2 dummy
#endif
error_e superloop_setup(void)
{
    AD1PCFG = 0xffff; //

    timer3_init();

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
    _TRISB13 = 1;
    _TRISB9 = 0;
    _TRISB7 = 1;
    _TRISB4 = 1;
    _TRISA4 = 1;
    _TRISA6 = 0;

    // Enable CN on selected pins
    CNEN1bits.CN0IE = 1;  // RA4
    CNEN1bits.CN1IE = 1;  // RB4
    CNEN2bits.CN23IE = 1; // RB7
    CNEN1bits.CN13IE = 1; // RB13

    CNPD1bits.CN0PDE = 1;  // RA4
    CNPD1bits.CN1PDE = 1;  // RB4
    CNPD2bits.CN23PDE = 1; // RB7
    CNPD1bits.CN13PDE = 1; // RB7

    // Configure interrupt
    IPC4bits.CNIP = 2;
    IFS1bits.CNIF = 0;
    IEC1bits.CNIE = 1;

    start_time = timer_millis();
    state_send_time = start_time;
    return ERROR_NONE;
}
static uint16_t prev_preset_value = 0;
static uint16_t prev_elapsed_value = 0;
error_e superloop_loop(void)
{

    while (0)
    {
        IDLE31KHZ();
        PORTB ^= 1 << 9;
    }

    const char *selected_message = NULL;
    uint16_t buttons;
    uint16_t now;

    now = timer_millis();
    buttons = _RB7 | _RB4 << 1 | _RA4 << 2;

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
        start_time = now;
        counter = 0;
        sendTestRigData();
    }
    else if (prev_preset_value != preset_value || elapsed_value != prev_elapsed_value){
        prev_preset_value = preset_value;
        prev_elapsed_value = elapsed_value;
        sendTestRigData();
    }
    preset_value = clamp_preset_value(preset_value);
    
    switch (state)
    {
    case TIMER_INIT_ENTRY:
        LED_1 = 0;
        LED_2 = 0;
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
        delay_ms = REPEAT_DELAY_MS;
        request_state = TIMER_IDLE;
        selected_message = messages[UART_SET_FORMAT];
        break;

    case TIMER_IDLE:
        LED_1 = 0;
        LED_2 = 0;
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
            Sleep();
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

        delay_ms = COUNT_DELAY_MS;
        request_state = TIMER_RUN;
        selected_message = messages[UART_COUNT_FORMAT];
        break;
    case TIMER_RUN:
        LED_2 = 0;
        if (elapsed_value == 0 || (buttons & 0x4) != 0)
        {
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_RUN_TRANSITION;
        }
        if (delay_elapsed(now))
        {
            LED_2 = !LED_2;
            elapsed_value--;
        }
        selected_message = messages[UART_COUNT_FORMAT];
        IDLE31KHZ();
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
        delay_ms = CLEAR_DELAY_MS;
        request_state = TIMER_PAUSE;
        selected_message = messages[UART_COUNT_PAUSED_FORMAT];
        break;

    case TIMER_PAUSE:
        if ((buttons & 0x4) != 0)
        {
            request_state = TIMER_PAUSE_TRANSITION;
        }
        Sleep();
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
        LED_1 = 0;
        LED_2 = 0;
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
        delay_ms = FAST_BLINK_MS;
        selected_message = messages[UART_ALARM_FORMAT];
        request_state = TIMER_ALARM;
        break;
    case TIMER_ALARM:
        LED_1 = 0;
        if (delay_elapsed(now))
        {
            LED_2 = !LED_2;
        }
        if ((buttons & 0x3) != 0)
        {
            request_state = TIMER_ALARM_TRANSITION;
        }
        IDLE31KHZ();
        break;
    case TIMER_ALARM_TRANSITION:
        LED_1 = 0;
        LED_2 = 0;
        request_state = TIMER_ALARM_TO_IDLE;
        break;
    case TIMER_ALARM_TO_IDLE:
        request_state = TIMER_IDLE_ENTRY;
        break;

    default:
        request_state = TIMER_INIT_ENTRY;
        break;
    }
    if (selected_message != NULL){
        uart_send_message(selected_message, elapsed_value, (uint16_t)preset_value);
    }
    return ERROR_NONE;
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    volatile uint16_t dummy_a = PORTA;
    volatile uint16_t dummy_b = PORTB;
    if (PORTBbits.RB13)
    {
        __asm__ volatile("reset");
    }
    (void)dummy_a;
    (void)dummy_b;

    IFS1bits.CNIF = 0;

    // Button state changed
}