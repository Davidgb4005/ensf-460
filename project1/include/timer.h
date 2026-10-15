#ifndef PIC24_TIMER_H
#define PIC24_TIMER_H
#include "xc.h"
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

extern volatile uint16_t ms_ticks;
uint16_t timer2_init(void);
uint16_t timer3_init(void);
uint16_t timer_millis(void);
#define CLOCKSPEED 8

#if CLOCKSPEED == 1
#define IDLE31KHZ()                             \
    do                                          \
    {                                           \
        while (!U2STAbits.TRMT)                 \
            ;                                   \
                                                \
        T3CONbits.TON = 0;                      \
                                                \
        __builtin_write_OSCCONH(0b101);         \
        __builtin_write_OSCCONL(OSCCON | 0x01); \
        while (OSCCONbits.OSWEN)                \
            ;                                   \
                                                \
        U2MODEbits.BRGH = 1;                    \
        U2BRG = 12;                             \
        U2MODEbits.USIDL = 0;                   \
                                                \
        T2CONbits.TON = 0;                      \
        TMR2 = 0;                               \
        IFS0bits.T2IF = 0;                      \
        T2CONbits.TON = 1;                      \
                                                \
        Idle();                                 \
    } while (0)

#endif

#if CLOCKSPEED == 32
#define ENABLE32MHZ()                           \
    do                                          \
    {                                           \
        if (U2MODEbits.UARTEN)                  \
            while (!U2STAbits.TRMT)             \
                ;                               \
        T3CONbits.TON = 0;                      \
        CLKDIVbits.RCDIV = 0;                   \
        __builtin_write_OSCCONH(0b001);         \
        __builtin_write_OSCCONL(OSCCON | 0x01); \
        while (OSCCONbits.OSWEN)                \
            ;                                   \
        while (!OSCCONbits.LOCK)                \
            ;                                   \
        U2BRG = 7;                              \
        TMR3 = 0;                               \
        PR3 = 15999;                            \
        IFS0bits.T3IF = 0;                      \
        T3CONbits.TON = 1;                      \
    } while (0)
#define IDLE31KHZ()                             \
    do                                          \
    {                                           \
        while (!U2STAbits.TRMT)                 \
            ;                                   \
        T3CONbits.TON = 0;                      \
        __builtin_write_OSCCONH(0b101);         \
        __builtin_write_OSCCONL(OSCCON | 0x01); \
        while (OSCCONbits.OSWEN)                \
            ;                                   \
        T2CONbits.TON = 1;                      \
        Idle();                                 \
        T2CONbits.TON = 0;                      \
        ENABLE32MHZ();                          \
    } while (0)
#endif

#if CLOCKSPEED == 8
#define ENABLE8MHZ()                            \
    do                                          \
    {                                           \
        if (U2MODEbits.UARTEN)                  \
            while (!U2STAbits.TRMT)             \
                ;                               \
        T3CONbits.TON = 0;                      \
        __builtin_write_OSCCONH(0b000);         \
        __builtin_write_OSCCONL(OSCCON | 0x01); \
        while (OSCCONbits.OSWEN)                \
            ;                                   \
        U2BRG = 1;                              \
        TMR3 = 0;                               \
        PR3 = 3999;                             \
        IFS0bits.T3IF = 0;                      \
        T3CONbits.TON = 1;                      \
    } while (0)
#define IDLE31KHZ()                             \
    do                                          \
    {                                           \
        while (!U2STAbits.TRMT)                 \
            ;                                   \
        T3CONbits.TON = 0;                      \
        __builtin_write_OSCCONH(0b101);         \
        __builtin_write_OSCCONL(OSCCON | 0x01); \
        while (OSCCONbits.OSWEN)                \
            ;                                   \
        T2CONbits.TON = 1;                      \
        Idle();                                 \
        T2CONbits.TON = 0;                      \
        ENABLE8MHZ();                           \
    } while (0)
#endif
//For 8 mhz TMR2 == 14617
//for 32mhz u 11800
#endif
