#ifndef PIC24_TIMER_H
#define PIC24_TIMER_H
#include <stdint.h>

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

void timer2_init(void);
void timer3_init(void);
uint16_t timer_millis(void);

void clock_set_31khz(void);
void clock_set_500khz(void);
void clock_set_4mhz(void);
void clock_set_8mhz(void);
void clock_set_32mhz(void);
void clock_idle_31khz(void);

/* The 500 kHz clock disables the 500 kbaud UART; faster setters restore it. */
#define SET31KHZ() clock_set_31khz()
#define SET500KHZ() clock_set_500khz()
#define SET4MHZ() clock_set_4mhz()
#define SET8MHZ() clock_set_8mhz()
#define SET32MHZ() clock_set_32mhz()
/* Sleep at 31 kHz, then restore the clock that was active before sleeping. */
#define IDLE31KHZ() clock_idle_31khz()

#endif
