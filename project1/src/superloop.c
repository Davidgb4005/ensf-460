#include <xc.h>

#include <stdint.h>
#include <stdio.h>

#include "superloop.h"
#include "system.h"
#include "timer.h"
#include "uart.h"





#define MAX_PRESET_VALUE 3599 // 59:99 max time as assigment
#define MIN_PRESET_VALUE 0
#define REPEAT_DELAY_MS 400 // set time will incmreent every 400 ms pb 1/2 or pb 1/3 2/3 are held
#define CLEAR_DELAY_MS 3000 // how long PB 1 and PB2 need to be held in idle to reset or PB 3 in pause to clear
#define COUNT_DELAY_MS 1000 // how fast led 1 blinks in run
#define FAST_BLINK_MS 250 // how fast led 2 blinks in ALARM

/*Waiting on these function from the UART module
void uart_init();
void uart_write_string(const char);
void uart_send_message(const char, uint16_t, uint16_t);
*/

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
typedef enum
{
    TIMER_INIT_ENTRY,
    TIMER_INIT,
    TIMER_INIT_TRANSITION,
    TIMER_INIT_TO_RESET,
    TIMER_IDLE_ENTRY,
    TIMER_IDLE,
    TIMER_IDLE_TRANSITION,
    TIMER_IDLE_TO_RUN,
    TIMER_IDLE_TO_RESET,
    TIMER_RESET_ENTRY,
    TIMER_RESET,
    TIMER_RESET_TRANSITION,
    TIMER_RESET_TO_IDLE,
    TIMER_RUN_ENTRY,
    TIMER_RUN,
    TIMER_RUN_TRANSITION,
    TIMER_RUN_TO_ALARM,
    TIMER_RUN_TO_PAUSE,
    TIMER_RUN_TO_CLEAR,
    TIMER_PAUSE_ENTRY,
    TIMER_PAUSE,
    TIMER_PAUSE_TRANSITION,
    TIMER_PAUSE_TO_RUN,
    TIMER_PAUSE_TO_CLEAR,
    TIMER_CLEAR_ENTRY,
    TIMER_CLEAR,
    TIMER_CLEAR_TRANSITION,
    TIMER_CLEAR_TO_IDLE,
    TIMER_ALARM_ENTRY,
    TIMER_ALARM,
    TIMER_ALARM_TRANSITION,
    TIMER_ALARM_TO_IDLE,
} timer_state_e;

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
    AD1PCFG = 0xffff;

    set_core_clock(default_core_clock);
    uart_init();
    U2MODEbits.BRGH = 1;
    U2BRG = (uint16_t)UART_BRG_VALUE(DEFAULT_CLOCK_HZ, DEFAULT_BAUD);
    ERROR_PRINT("%s", "UART Setup Complete");

    timer2_init();
    ERROR_PRINT("%s", "Timer 2 Setup Complete");

    _TRISB12 = 0;
    _LATB12 = 0;
    _TRISB14 = 1;
    _TRISB9 = 0;
    _TRISB7 = 1;
    _TRISB4 = 1;
    _TRISA4 = 1;
    _TRISA6 = 0;

    CNEN1bits.CN0IE = 1;
    CNEN1bits.CN1IE = 1;
    CNEN2bits.CN23IE = 1;
    CNEN1bits.CN13IE = 1;

    CNPD1bits.CN0PDE = 1;
    CNPD1bits.CN1PDE = 1;
    CNPD2bits.CN23PDE = 1;
    CNPD1bits.CN13PDE = 1;

    IPC4bits.CNIP = 2;
    IFS1bits.CNIF = 0;
    IEC1bits.CNIE = 1;

    start_time = millis();
}

void superloop_loop(void)
{
    const char *selected_message = NULL;
    uint32_t now = millis();

    if (buttons != prev_buttons)
    {
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
    }
    else if (prev_preset_value != preset_value || prev_elapsed_value != elapsed_value)
    {
        prev_preset_value = preset_value;
        prev_elapsed_value = elapsed_value;
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
        break;

    case TIMER_IDLE:
        LED_1 = 0;
        LED_2 = 0;
        switch (buttons)
        {
        case 0x1:
        case 0x2:
        case 0x5:
        case 0x6:
        case 0x3:
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_IDLE_TRANSITION;
            break;
        case 0x7:
            break;
        default:
            Sleep();
            break;
        }
        break;

    case TIMER_IDLE_TRANSITION:
        if (buttons & 0x3)
        {
            if (delay_elapsed//delay 3000)
            {
                request_state = TIMER_IDLE_TO_RESET;
            }
        }
        else if (elapsed_value > 0)
        {
            request_state = TIMER_IDLE_TO_RUN;
        }
        else //  if the preset is 0 go to idle
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
        request_state = TIMER_RESET_TRANSITION;
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
        break;
    case TIMER_RUN:
        LED_2 = 0;
        _LATB12 = OUTPUT_CONTROL;
        if (elapsed_value == 0 || (buttons & 0x4u) != 0u)
        {
            _LATB12 = 0;
            delay_ms = CLEAR_DELAY_MS;
            request_state = TIMER_RUN_TRANSITION;
        }
        if (delay_elapsed(now))
        {
            LED_1 = !LED_1;
            if (elapsed_value > 0)
            {
                elapsed_value--;
            }
            selected_message = messages[UART_COUNT_FORMAT];
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
        break;
    case TIMER_PAUSE:
        if ((buttons & 0x4u) != 0u)
        {
            request_state = TIMER_PAUSE_TRANSITION;
        }
        Sleep();
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
        //uart_send_message(selected_message, elapsed_value, (uint16_t)preset_value);
        //Transmission needs to be fixed
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
}
