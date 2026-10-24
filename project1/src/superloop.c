#include <xc.h>

#include <stdint.h>
#include <stdio.h>

#include "superloop.h"
#include "system.h"
#include "timer.h"
#include "uart.h"
#include "delay.h"

#define DEBUG 0

typedef enum
{
    UART_COUNT_FORMAT,
    UART_COUNT_PAUSED_FORMAT,
    UART_ALARM_FORMAT,
    UART_SET_FORMAT,
    UART_INFO_FORMAT,
    UART_CLEAR_FORMAT,
    UART_NONE
} uart_message_e;

#define MAX_PRESET_VALUE 3599 // 59:99 max time as assigment
#define MIN_PRESET_VALUE 0
#define REPEAT_DELAY_MS 500 // set time will incmreent every 400 ms pb 1/2 or pb 1/3 2/3 are held
#define CLEAR_DELAY_MS 3000 // how long PB 1 and PB2 need to be held in idle to reset or PB 3 in pause to clear
#define COUNT_DELAY_MS 1000 // how fast led 1 blinks in run
#define FAST_BLINK_MS 250   // how fast led 2 blinks in ALARM

#if DEBUG
#define LINE_CLEAR "\n"
#else

#define LINE_CLEAR "\r\033[K"
#endif

#define INCREASE_INCREMENT_THRESHOLD 10

static timer_state_e state = TIMER_INIT;
static uint16_t elapsed_value;

static timer_state_e request_state = TIMER_INIT;
static uint16_t counter;
static uint16_t prev_buttons;
static timer_state_e prev_state = TIMER_IDLE;
static int16_t preset_mins;
static int16_t preset_secs;
static uint16_t prev_elapsed_value;
static uint32_t elapsed_before_pause;

static uart_message_e selected_message;

static volatile uint16_t buttons;

static delay_s delay;

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

#define LED_1 _LATB9
#define LED_2 _LATA6

#define delay_start(elapse_time)           \
    do                                     \
    {                                      \
        delay_init(&delay, (elapse_time)); \
        delay_enable(&delay, millis());    \
    } while (0)

#define print(fmt, ...)               \
    do                                \
    {                                 \
        uart_enable();                \
        printf((fmt), ##__VA_ARGS__); \
        uart_disable();               \
    } while (0)

#define print_debug(fmt, ...)              \
    do                                     \
    {                                      \
        uart_enable();                     \
        debug_print((fmt), ##__VA_ARGS__); \
        uart_disable();                    \
    } while (0)

void superloop_setup(void)
{
    set_core_clock(FREQ_8_MHZ);

    AD1PCFG = 0xffff;

    uart_init(500000); //Starts Uart at 500k baud
    uart_disable(); // Disables Uart till transmission
    timer_init(FREQ_8_MHZ); // Inits Timers to use the 8MHZ source clock
    /*GPIO Init Sets all needed bits to correctly run RA6/RB9 as outputs and RA4/RB4/RB7 as inputs
    */
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

    print("SET %02dm : %02ds", preset_mins, preset_secs);
}

void superloop_loop(void)
{
    selected_message = UART_NONE;//message that gets selected by state machine for printing

    if (buttons != prev_buttons) // If the buttons have changed since last cycle reset the hold counter
    {
        prev_buttons = buttons;
        counter = 0;
    }


    elapsed_value = (uint16_t)clamp_preset_value((int16_t)elapsed_value); // Clamps the elapsed value between 3599 and 0 (59:59/0)

    if (request_state != state) // If a state request has been logged service it and reset hold counter
    {
        prev_state = state;
        state = request_state;
        counter = 0;
    }

    if (prev_elapsed_value != elapsed_value)// If the elapsed value has change set the selected message to print it to terminal
    {
        selected_message = UART_COUNT_FORMAT;
        prev_elapsed_value = elapsed_value;
    }

    switch (state) // Main State Machine loop
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
        delay_start(REPEAT_DELAY_MS);
        request_state = TIMER_IDLE;
        break;

    case TIMER_IDLE:
        LED_1 = 0;
        LED_2 = 0;
        switch (buttons)
        {
        case 0x1:
            if (delay_elapsed(&delay))
            {
                counter++;
                preset_secs += counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1;
                preset_secs = preset_secs > 59 ? 0 : preset_secs;
                selected_message = UART_SET_FORMAT;
            }
            break;
        case 0x2:
            if (delay_elapsed(&delay))
            {
                counter++;
                preset_mins += counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1;
                preset_mins = preset_mins > 59 ? 59 : preset_mins;
                selected_message = UART_SET_FORMAT;
            }
            break;
        case 0x5:
            if (delay_elapsed(&delay))
            {
                counter++;
                preset_secs -= counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1;
                preset_secs = preset_secs < 0 ? 0 : preset_secs;
                selected_message = UART_SET_FORMAT;
            }
            break;
        case 0x6:
            if (delay_elapsed(&delay))
            {
                counter++;
                preset_mins -= counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1;
                preset_mins = preset_mins < 0 ? 0 : preset_mins;
                selected_message = UART_SET_FORMAT; 
            }
            break;
        case 0x3:
            delay_start(CLEAR_DELAY_MS);
            request_state = TIMER_IDLE_TRANSITION;
            break;
        case 0x7:
            selected_message = UART_INFO_FORMAT;
            delay_start(REPEAT_DELAY_MS);
            break;
        default:
            counter = 0;
            Sleep();
            break;
        }
        break;

    case TIMER_IDLE_TRANSITION:
        if (buttons == 3)
        {
            if (delay_elapsed(&delay))
            {
                request_state = TIMER_IDLE_TO_RESET;
            }
        }
        else if (preset_mins > 0 || preset_secs > 0)
        {
            request_state = TIMER_IDLE_TO_RUN;
        }
        else //  if the preset is 0 go to idle
        {
            request_state = TIMER_IDLE_ENTRY;
        }
        break;
    case TIMER_IDLE_TO_RUN:
        elapsed_value = (uint16_t)(preset_mins * 60 + preset_secs);
        request_state = TIMER_RUN_ENTRY;
        break;
    case TIMER_IDLE_TO_RESET:
        preset_mins = 0;
        preset_secs = 0;
        request_state = TIMER_RESET_ENTRY;
        selected_message = UART_SET_FORMAT;
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
        delay_init(&delay, COUNT_DELAY_MS);
        delay_enable(&delay, millis() - elapsed_before_pause);
        elapsed_before_pause = 0;
        request_state = TIMER_RUN;
        break;
    case TIMER_RUN:
        LED_2 = 0;
        if (elapsed_value == 0 || (buttons & 0x4u) != 0u)
        {
            request_state = TIMER_RUN_TRANSITION;
            elapsed_before_pause = millis() - delay.start_ms;
            delay_start(CLEAR_DELAY_MS);
        }
        if (delay_elapsed(&delay))
        {
            LED_1 = !LED_1;
            if (elapsed_value > 0)
            {
                elapsed_value--;
            }
        }
        idle_low_power();
        break;

    case TIMER_RUN_TRANSITION:
        if (elapsed_value == 0)
        {
            request_state = TIMER_RUN_TO_ALARM;
        }
        else if ((buttons & 0x4u) != 0u)
        {
            if (delay_elapsed(&delay))
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
        request_state = TIMER_PAUSE;
        selected_message = UART_COUNT_PAUSED_FORMAT;

        break;
    case TIMER_PAUSE:
        if ((buttons & 0x4u) > 0)
        {
            request_state = TIMER_PAUSE_TRANSITION;
            delay_start(CLEAR_DELAY_MS);
        }
        else
        {
            Sleep();
        }
        break;
    case TIMER_PAUSE_TRANSITION:
        if ((buttons & 0x4) != 0)
        {
            if (delay_elapsed(&delay))
            {
                request_state = TIMER_PAUSE_TO_CLEAR;
            }
        }
        else
        {
            selected_message = UART_COUNT_FORMAT;
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
        elapsed_value = 0;
        preset_secs = 0;
        preset_mins = 0;
        request_state = TIMER_CLEAR;
        break;
    case TIMER_CLEAR:
        if (buttons)
        {
            request_state = TIMER_CLEAR_TRANSITION;
        }
        selected_message = UART_CLEAR_FORMAT;
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
        delay_start(FAST_BLINK_MS);
        selected_message = UART_ALARM_FORMAT;
        request_state = TIMER_ALARM;
        break;
    case TIMER_ALARM:
        LED_1 = 1;
        if (delay_elapsed(&delay))
        {
            LED_2 = !LED_2;
        }
        if ((buttons & 0x3u) != 0u)
        {
            request_state = TIMER_ALARM_TRANSITION;
            selected_message = UART_SET_FORMAT;
        }
        idle_low_power();
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

    elapsed_value = (uint16_t)clamp_preset_value((int16_t)elapsed_value);
    switch (selected_message)
    {
    case UART_COUNT_FORMAT:
        print("%sCNT %02dm : %02ds", LINE_CLEAR, elapsed_value / 60, elapsed_value % 60);
        break;
    case UART_ALARM_FORMAT:
        print("%sFIN 00m : 00s - ALARM", LINE_CLEAR);
        break;
    case UART_COUNT_PAUSED_FORMAT:
        print("%sCNT %02dm : %02ds {PAUSED}", LINE_CLEAR, elapsed_value / 60, elapsed_value % 60);
        break;
    case UART_INFO_FORMAT:
        print("%s2026 ENSF 460 L01 - Group 06", LINE_CLEAR);
        Sleep();
        break;
    case UART_SET_FORMAT:
        print("%sSET %02dm : %02ds", LINE_CLEAR, preset_mins, preset_secs);
        break;
    case UART_CLEAR_FORMAT:
        print("%sCLR 00m : 00s", LINE_CLEAR);
        break;
    default:
        break;
    }
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    volatile uint16_t dummy_a = PORTA;
    volatile uint16_t dummy_b = PORTB;

    buttons = (uint16_t)_RB7 | ((uint16_t)_RB4 << 1) | ((uint16_t)_RA4 << 2);

    (void)dummy_a;
    (void)dummy_b;
    IFS1bits.CNIF = 0;
}
