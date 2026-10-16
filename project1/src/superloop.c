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


error_e superloop_setup(void)
{
    AD1PCFG = 0xffff; // Disable AN11; enable RB13 digital input
}

error_e superloop_loop(void)
{



    for (uint16_t i = 0; i < ARRAY_COUNT(push_button); i++)
    {
        debounce_read(&push_button[i]);
    }

    now = timer_millis();
    buttons = read_control_register();


    switch (state)
    {
    case TIMER_INIT_ENTRY:
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
        request_state = TIMER_IDLE;
        break;

    case TIMER_IDLE:
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
            break;
        }
        break;

    case TIMER_IDLE_TRANSITION:
        if (//if pb 1 and 2 are pressed wait for elapsed delay then reset)
        {
            if (//delay 3000)
            {
                request_state = TIMER_IDLE_TO_RESET;
            }
        }
        else if (// if the preset value is not 0 run)
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
        request_state = TIMER_RUN;
        break;
    case TIMER_RUN:
        break;

    case TIMER_RUN_TRANSITION:
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
        break;

    case TIMER_PAUSE:
        request_state = TIMER_PAUSE_TRANSITION;
        break;

    case TIMER_PAUSE_TRANSITION:
        break;
    case TIMER_PAUSE_TO_RUN:
        request_state = TIMER_RUN_ENTRY;
        break;
    case TIMER_PAUSE_TO_CLEAR:
        request_state = TIMER_CLEAR_ENTRY;
        break;

    case TIMER_CLEAR_ENTRY:
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
        request_state = TIMER_ALARM;
        break;
    case TIMER_ALARM:
        break;
    case TIMER_ALARM_TRANSITION:
        request_state = TIMER_ALARM_TO_IDLE;
        break;
    case TIMER_ALARM_TO_IDLE:
        request_state = TIMER_IDLE_ENTRY;
        break;

    default:
        request_state = TIMER_INIT_ENTRY;
        break;
    }


    return ERROR_NONE;
}
