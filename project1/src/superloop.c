#include <xc.h>

#include <stdint.h>
#include <stdio.h>

#include "superloop.h"
#include "system.h"
#include "timer.h"
#include "uart.h"

#if DEFAULT_CLOCK_HZ == CLOCK_32MHZ
#define DEFAULT_CORE_CLOCK FREQ_32_MHZ
#define DEFAULT_BAUD BAUD32MHZ
#elif DEFAULT_CLOCK_HZ == CLOCK_8MHZ
#define DEFAULT_CORE_CLOCK FREQ_8_MHZ
#define DEFAULT_BAUD BAUD8MHZ
#elif DEFAULT_CLOCK_HZ == CLOCK_4MHZ
#define DEFAULT_CORE_CLOCK FREQ_4_MHZ
#define DEFAULT_BAUD BAUD4MHZ
#elif DEFAULT_CLOCK_HZ == CLOCK_1MHZ
#define DEFAULT_CORE_CLOCK FREQ_1_MHZ
#define DEFAULT_BAUD BAUD1MHZ
#elif DEFAULT_CLOCK_HZ == CLOCK_500KHZ
#define DEFAULT_CORE_CLOCK FREQ_500_KHZ
#define DEFAULT_BAUD BAUD500KHZ
#elif DEFAULT_CLOCK_HZ == CLOCK_32KHZ
#define DEFAULT_CORE_CLOCK FREQ_32_KHZ
#define DEFAULT_BAUD BAUD32KHZ
#else
#error "DEFAULT_CLOCK_HZ must be 32000000, 8000000, 4000000, 1000000, 500000, or 32000"
#endif

#define IDLE_LOW_POWER()                    \
    do                                      \
    {                                       \
        uart_shutdown();                    \
        set_core_clock(FREQ_32_KHZ);        \
        Idle();                             \
        set_core_clock(default_core_clock); \
    } while (0)

#define SLEEP_WHEN_UART_IDLE() \
    do                         \
    {                          \
        uart_shutdown();       \
        Sleep();               \
    } while (0)

#define ERROR_PRINT(fmt, ...)                                                                       \
    do                                                                                              \
    {                                                                                               \
        snprintf(err_msg, sizeof(err_msg), " %s,Line %d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        uart_write_string(err_msg);                                                                 \
    } while (0)

#define MAX_PRESET_VALUE 3599
#define MIN_PRESET_VALUE 0
#define REPEAT_DELAY_MS 400
#define CLEAR_DELAY_MS 3000
#define COUNT_DELAY_MS 1000
#define FAST_BLINK_MS 250
#define HARDWARE_OUTPUTS_ENABLED 1
static const core_clock_freq_e default_core_clock = DEFAULT_CORE_CLOCK;
static char err_msg[128];

volatile timer_state_e state = TIMER_INIT;
volatile uint16_t elapsed_value;

static timer_state_e request_state = TIMER_INIT;
static int16_t preset_value;
static uint16_t counter;
static uint16_t prev_buttons;
static uint16_t start_time;
static uint16_t delay_ms = REPEAT_DELAY_MS;
static timer_state_e prev_state = TIMER_IDLE;
static test_rig_data_s test_rig_data;
static int16_t prev_preset_value;
static uint16_t prev_elapsed_value;

static volatile uint16_t buttons;

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

static void send_test_rig_data(void)
{
    test_rig_data.elapsed_seconds = elapsed_value;
    test_rig_data.preset_seconds = (uint16_t)preset_value;
    test_rig_data.states.state_code = (uint16_t)state;
    test_rig_data.states.prev_state_code = (uint16_t)prev_state;
    test_rig_data.gpio.led_1_state = _RB9;
    test_rig_data.gpio.led_2_state = _RA6;
    test_rig_data.gpio.pb_1 = (buttons & 0x1u) != 0u;
    test_rig_data.gpio.pb_2 = (buttons & 0x2u) != 0u;
    test_rig_data.gpio.pb_3 = (buttons & 0x4u) != 0u;
    uart_send_state(&test_rig_data);
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

#if HARDWARE_OUTPUTS_ENABLED
#define LED_1 _LATB9
#define LED_2 _LATA6
#define OUTPUT_CONTROL 1
#else
static uint16_t dummy_output;
#define LED_1 dummy_output
#define LED_2 dummy_output
#define OUTPUT_CONTROL 0
#endif

void superloop_setup(void)
{
    AD1PCFG = 0u;
    AD1PCFGbits.PCFG2 = 1;  // AN2/RB0: UART2 TX
    AD1PCFGbits.PCFG3 = 1;  // AN3/RB1: UART2 RX
    AD1PCFGbits.PCFG11 = 1; // AN11/RB13: test-rig reset input
    AD1PCFGbits.PCFG12 = 1; // AN12/RB12: output control

    PMD1bits.ADC1MD = 1;
    PMD1bits.SPI1MD = 1;
    PMD1bits.U1MD = 1;
    PMD1bits.I2C1MD = 1;
    PMD1bits.T1MD = 1;
    PMD2bits.OC1MD = 1;
    PMD2bits.IC1MD = 1;
    PMD3bits.CRCPMD = 1;
    PMD3bits.RTCCMD = 1;
    PMD3bits.CMPMD = 1;
    PMD4bits.HLVDMD = 1;
    PMD4bits.CTMUMD = 1;
    PMD4bits.REFOMD = 1;
    PMD4bits.EEMD = 1;

    set_core_clock(default_core_clock);
    uart_init(DEFAULT_BAUD);
    // ERROR_PRINT("%s", "UART Setup Complete");
    timer2_init();
    timer3_init();
    // ERROR_PRINT("%s", "Timer 2 Setup Complete");
    _TRISB12 = 0;
    _LATB12 = 0;
    _TRISB8 = 0;
    _LATB8 = 0;
    _TRISB14 = 1;
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
    CNPD1bits.CN13PDE = 1; // RB13

    // Configure interrupt
    IPC4bits.CNIP = 2;
    IFS1bits.CNIF = 0;
    IEC1bits.CNIE = 1;

    start_time = timer_millis();
}

void superloop_loop(void)
{
    const char *selected_message = NULL;
    uint16_t now = timer_millis();

    if (buttons != prev_buttons)
    {
        timer2_reset_phase();
        prev_buttons = buttons;
        counter = 0;
        start_time = now;
    }

    elapsed_value = (uint16_t)clamp_preset_value((int16_t)elapsed_value);

    if (request_state != state)
    {
        prev_state = state;
        state = request_state;
        start_time = now;
        counter = 0;
        send_test_rig_data();
    }
    else if (prev_preset_value != preset_value || prev_elapsed_value != elapsed_value)
    {
        prev_preset_value = preset_value;
        prev_elapsed_value = elapsed_value;
        send_test_rig_data();
    }

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
            counter++;
            preset_value += counter > 20 ? 5 : 1;
            selected_message = messages[UART_SET_FORMAT];
            IDLE_LOW_POWER();
            break;
        case 0x2:
            counter++;
            preset_value += counter > 20 ? 300 : 60;
            selected_message = messages[UART_SET_FORMAT];
            IDLE_LOW_POWER();
            break;
        case 0x5:
            counter++;
            preset_value -= counter > 20 ? 5 : 1;
            selected_message = messages[UART_SET_FORMAT];
            IDLE_LOW_POWER();
            break;
        case 0x6:
            counter++;
            preset_value -= counter > 20 ? 300 : 60;
            selected_message = messages[UART_SET_FORMAT];
            IDLE_LOW_POWER();
            break;
        case 0x3:
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_IDLE_TRANSITION;
            break;
        case 0x7:
            selected_message = messages[UART_INFO_FORMAT];
            break;
        default:
            SLEEP_WHEN_UART_IDLE();
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
        LED_1 = 0;
        LED_2 = 0;
        _LATB12 = OUTPUT_CONTROL;
        delay_ms = COUNT_DELAY_MS;
        request_state = TIMER_RUN;
        selected_message = messages[UART_COUNT_FORMAT];
        timer2_reset_phase();
        break;
    case TIMER_RUN:
        LED_2 = 0;
        _LATB12 = OUTPUT_CONTROL;
        if (delay_elapsed(now))
        {
            LED_1 = !LED_1;
            if (elapsed_value > 0u)
            {
                elapsed_value--;
            }
            selected_message = messages[UART_COUNT_FORMAT];
        }
        if (elapsed_value == 0 || (buttons & 0x4u) != 0u)
        {
            _LATB12 = 0;
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_RUN_TRANSITION;
        }
        IDLE_LOW_POWER();
        break;

    case TIMER_RUN_TRANSITION:
        if (elapsed_value == 0)
        {
            request_state = TIMER_RUN_TO_ALARM;
        }
        else if ((buttons & 0x4u) != 0u)
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
        if ((buttons & 0x4u) != 0u)
        {
            request_state = TIMER_PAUSE_TRANSITION;
        }
        SLEEP_WHEN_UART_IDLE();
        break;

    case TIMER_PAUSE_TRANSITION:
        if ((buttons & 0x4u) != 0u)
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
        LED_1 = 1;
        LED_2 = 0;
        delay_ms = FAST_BLINK_MS;
        selected_message = messages[UART_ALARM_FORMAT];
        request_state = TIMER_ALARM;
        break;
    case TIMER_ALARM:
        LED_1 = 1;
        if (delay_elapsed(now))
        {
            LED_2 = !LED_2;
        }
        if ((buttons & 0x3u) != 0u)
        {
            request_state = TIMER_ALARM_TRANSITION;
        }
        IDLE_LOW_POWER();
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
    if (selected_message != NULL)
    {
        uart_send_message(selected_message, elapsed_value, (uint16_t)preset_value);
    }
    preset_value = clamp_preset_value(preset_value);
    elapsed_value = (uint16_t)clamp_preset_value((int16_t)elapsed_value);
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    volatile uint16_t dummy_a = PORTA;
    volatile uint16_t dummy_b = PORTB;
    if (PORTBbits.RB13)
    {
        __asm__ volatile("reset");
    }

    buttons = (uint16_t)_RB7 | ((uint16_t)_RB4 << 1) | ((uint16_t)_RA4 << 2);
    (void)dummy_a;
    (void)dummy_b;

    IFS1bits.CNIF = 0;

    // Button state changed
}
