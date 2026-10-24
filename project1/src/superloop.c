#include <xc.h>

#include <stdint.h>
#include <stdio.h>

#include "superloop.h"
#include "system.h"
#include "timer.h"
#include "uart.h"
#include "delay.h"

#define DEBUG 0 // Selects terminal control codes or newline debug output.

typedef enum
{
    UART_COUNT_FORMAT,        // Displays the active countdown value.
    UART_COUNT_PAUSED_FORMAT, // Displays the paused countdown value.
    UART_ALARM_FORMAT,        // Displays the completed alarm message.
    UART_SET_FORMAT,          // Displays the configured preset value.
    UART_INFO_FORMAT,         // Displays the group information.
    UART_CLEAR_FORMAT,        // Displays the cleared timer message.
    UART_NONE                 // Suppresses UART output for this iteration.
} uart_message_e;

typedef enum
{
    PB_NONE = 0x0,    // No push button is pressed.
    PB1 = 0x1,        // Only PB1 is pressed.
    PB2 = 0x2,        // Only PB2 is pressed.
    PB1_PB2 = 0x3,    // PB1 and PB2 are pressed.
    PB3 = 0x4,        // Only PB3 is pressed.
    PB1_PB3 = 0x5,    // PB1 and PB3 are pressed.
    PB2_PB3 = 0x6,    // PB2 and PB3 are pressed.
    PB1_PB2_PB3 = 0x7 // All push buttons are pressed.
} button_state_e;

#define MAX_PRESET_VALUE 3599 // Maximum countdown value in seconds.
#define MIN_PRESET_VALUE 0    // Minimum countdown value in seconds.
#define REPEAT_DELAY_MS 500   // Sets the held-button repeat interval.
#define CLEAR_DELAY_MS 3000   // Sets the long-press clear interval.
#define COUNT_DELAY_MS 1000   // Sets the countdown and LED1 interval.
#define FAST_BLINK_MS 250     // Sets the alarm LED2 blink interval.

#if DEBUG
#define LINE_CLEAR "\n" // Starts a new line during debug output.
#else

#define LINE_CLEAR "\r\033[K" // Clears and rewrites the terminal line.
#endif

#define INCREASE_INCREMENT_THRESHOLD 10 // Selects when held adjustments change to steps of five.

static timer_state_e state = TIMER_INIT_ENTRY; // Stores the active state-machine state.
static uint16_t elapsed_value;                 // Stores the remaining countdown seconds.

static timer_state_e request_state = TIMER_INIT_ENTRY; // Stores the next requested state.
static uint16_t counter;                               // Counts repeated held-button adjustments.
static uint16_t prev_buttons;                          // Stores the previous button combination.
static timer_state_e prev_state = TIMER_IDLE;          // Stores the previous state for diagnostics.
static int16_t preset_mins;                            // Stores the configured minutes.
static int16_t preset_secs;                            // Stores the configured seconds.
static uint16_t prev_elapsed_value;                    // Stores the last displayed countdown value.
static uint32_t elapsed_before_pause;                  // Stores partial countdown time across a pause.

static uart_message_e selected_message; // Stores the UART message selected this iteration.

static volatile uint16_t buttons; // Stores the ISR-updated button bit mask.

static delay_s delay; // Provides the shared non-blocking interval timer.

static int16_t clamp_preset_value(int16_t value) // Constrains a value to the supported countdown range.
{
    if (value > MAX_PRESET_VALUE) // Clamp values above the supported maximum.
    {
        return MAX_PRESET_VALUE;
    }
    if (value < MIN_PRESET_VALUE) // Clamp values below zero.
    {
        return MIN_PRESET_VALUE;
    }
    return value; // Return values already inside the valid range.
}

#define LED_1 _LATB9 // Maps LED1 to its output latch.
#define LED_2 _LATA6 // Maps LED2 to its output latch.

// Starts the shared delay from the current time.
#define delay_start(elapse_time)           \
    do                                     \
    {                                      \
        delay_init(&delay, (elapse_time)); \
        delay_enable(&delay, millis());    \
    } while (0)

// Enables UART only while transmitting.
#define print(fmt, ...)               \
    do                                \
    {                                 \
        uart_enable();                \
        printf((fmt), ##__VA_ARGS__); \
        uart_disable();               \
    } while (0)

// Enables UART only while transmitting debug text.
#define print_debug(fmt, ...)              \
    do                                     \
    {                                      \
        uart_enable();                     \
        debug_print((fmt), ##__VA_ARGS__); \
        uart_disable();                    \
    } while (0)

void superloop_setup(void) // Configures clocks, peripherals, GPIO, and interrupts.
{
    set_core_clock(FREQ_8_MHZ); // Select the normal operating clock.

    AD1PCFG = 0xffff; // Configure analog-capable pins as digital I/O.

    uart_init(500000);      // Initialize UART at 500 kbaud.
    uart_disable();         // Disable UART between transmissions.
    timer_init(FREQ_8_MHZ); // Initialize timing for the 8 MHz clock.

    _TRISB12 = 0; // Configure RB12 as an output.
    _LATB12 = 0;  // Drive RB12 low initially.
    _TRISB14 = 1; // Configure RB14 as an input.
    _TRISB9 = 0;  // Configure LED1 as an output.
    _TRISB7 = 1;  // Configure the PB1 pin as an input.
    _TRISB4 = 1;  // Configure the mapped push-button pin as an input.
    _TRISA4 = 1;  // Configure the mapped push-button pin as an input.
    _TRISA6 = 0;  // Configure LED2 as an output.

    CNEN1bits.CN0IE = 1;   // Enable change notification on CN0.
    CNEN1bits.CN1IE = 1;   // Enable change notification on CN1.
    CNEN2bits.CN23IE = 1;  // Enable change notification on CN23.
    CNEN1bits.CN13IE = 1;  // Enable change notification on CN13.
    CNPD1bits.CN0PDE = 1;  // Enable the CN0 pull-down resistor.
    CNPD1bits.CN1PDE = 1;  // Enable the CN1 pull-down resistor.
    CNPD2bits.CN23PDE = 1; // Enable the CN23 pull-down resistor.
    CNPD1bits.CN13PDE = 1; // Enable the CN13 pull-down resistor.
    IPC4bits.CNIP = 2;     // Set the CN interrupt priority.
    IFS1bits.CNIF = 0;     // Clear any pending CN interrupt flag.
    IEC1bits.CNIE = 1;     // Enable CN interrupts.

    print("SET %02dm : %02ds", preset_mins, preset_secs); // Display the initial preset.
}

void superloop_loop(void) // Executes one iteration of the timer state machine.
{
    selected_message = UART_NONE; // Default to no UART output.

    if (buttons != prev_buttons) // Detect a new button combination.
    {
        prev_buttons = buttons; // Save the new button combination.
        counter = 0;            // Restart held-button acceleration.
    }

    if (request_state != state) // Apply a pending state transition.
    {
        prev_state = state;    // Preserve the state being exited.
        state = request_state; // Enter the requested state.
        counter = 0;           // Restart held-button acceleration.
    }

    if (prev_elapsed_value != elapsed_value) // Detect a changed countdown value.
    {
        selected_message = UART_COUNT_FORMAT; // Request a countdown update.
        prev_elapsed_value = elapsed_value;   // Remember the displayed value.
    }

    switch (state) // Execute the active state.
    {
    case TIMER_INIT_ENTRY:                // Initialize timer outputs.
        LED_1 = 0;                        // Start with LED1 off.
        LED_2 = 0;                        // Start with LED2 off.
        request_state = TIMER_IDLE_ENTRY; // Continue into idle setup.
        break;

    case TIMER_IDLE_ENTRY:            // Prepare the setting state.
        delay_start(REPEAT_DELAY_MS); // Start the adjustment repeat timer.
        request_state = TIMER_IDLE;   // Continue into the idle state.
        break;

    case TIMER_IDLE:     // Process preset controls.
        LED_1 = 0;       // Keep LED1 off while setting.
        LED_2 = 0;       // Keep LED2 off while setting.
        switch (buttons) // Select the active button combination.
        {
        case PB1:                      // Increase preset seconds.
            if (delay_elapsed(&delay)) // Wait for the hold interval.
            {
                counter++; // Count this repeated adjustment.

                preset_secs += counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1; // Accelerate after the threshold.

                preset_secs %= 60; // Wrap seconds after 59.

                selected_message = UART_SET_FORMAT; // Request a preset update.
            }
            break;

        case PB2:                      // Increase preset minutes.
            if (delay_elapsed(&delay)) // Wait for the hold interval.
            {
                counter++; // Count this repeated adjustment.

                preset_mins += counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1; // Accelerate after the threshold.

                preset_mins = preset_mins > 59 ? 59 : preset_mins; // Limit minutes to 59.

                selected_message = UART_SET_FORMAT; // Request a preset update.
            }
            break;

        case PB1_PB3:                  // Decrease preset seconds.
            if (delay_elapsed(&delay)) // Wait for the hold interval.
            {
                counter++; // Count this repeated adjustment.

                preset_secs -= counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1; // Accelerate after the threshold.

                preset_secs = preset_secs < 0 ? 59 : preset_secs; // Clamp seconds at zero.

                selected_message = UART_SET_FORMAT; // Request a preset update.
            }
            break;

        case PB2_PB3:                  // Decrease preset minutes.
            if (delay_elapsed(&delay)) // Wait for the hold interval.
            {
                counter++; // Count this repeated adjustment.

                preset_mins -= counter > INCREASE_INCREMENT_THRESHOLD ? 5 : 1; // Accelerate after the threshold.

                preset_mins = preset_mins < 0 ? 0 : preset_mins; // Clamp minutes at zero.

                selected_message = UART_SET_FORMAT; // Request a preset update.
            }
            break;

        case PB1_PB2:                              // Detect start or preset clear.
            delay_start(CLEAR_DELAY_MS);           // Start long-press detection.
            request_state = TIMER_IDLE_TRANSITION; // Wait for release or timeout.
            break;

        case PB1_PB2_PB3:                        // Display group information.
            selected_message = UART_INFO_FORMAT; // Request the information message.
            delay_start(REPEAT_DELAY_MS);        // Restart the shared delay.
            break;

        default:         // Handle PB3 alone or no buttons.
            counter = 0; // Disable held-button acceleration.
            Sleep();     // Sleep until an interrupt occurs.
            break;
        }
        break;

    case TIMER_IDLE_TRANSITION: // Distinguish start from preset clear.
        if (buttons == 3)       // Continue checking a held PB1 and PB2.
        {
            if (delay_elapsed(&delay)) // Detect the long-press threshold.
            {
                request_state = TIMER_IDLE_TO_RESET; // Clear the preset.
            }
        }
        else if (preset_mins > 0 || preset_secs > 0) // Reject a zero-length countdown.
        {
            request_state = TIMER_IDLE_TO_RUN; // Start a nonzero countdown.
        }
        else // Return to setting for a zero preset.
        {
            request_state = TIMER_IDLE_ENTRY; // Re-enter idle setup.
        }
        break;

    case TIMER_IDLE_TO_RUN:                                         // Transfer the preset into the countdown.
        elapsed_value = (uint16_t)(preset_mins * 60 + preset_secs); // Convert the preset to seconds.
        request_state = TIMER_RUN_ENTRY;                            // Prepare the running state.
        break;

    case TIMER_IDLE_TO_RESET:               // Clear the configured preset.
        preset_mins = 0;                    // Clear preset minutes.
        preset_secs = 0;                    // Clear preset seconds.
        request_state = TIMER_IDLE_ENTRY;   // Return to idle setup.
        selected_message = UART_SET_FORMAT; // Display the cleared preset.
        break;

    case TIMER_RUN_ENTRY:                                      // Prepare or resume the countdown.
        LED_1 = 0;                                             // Start the run blink cycle with LED1 off.
        LED_2 = 0;                                             // Keep the alarm LED off while running.
        delay_init(&delay, COUNT_DELAY_MS);                    // Configure the one-second interval.
        delay_enable(&delay, millis() - elapsed_before_pause); // Restore the partial interval.
        elapsed_before_pause = 0;                              // Clear the saved partial interval.
        request_state = TIMER_RUN;                             // Continue into the running state.
        break;

    case TIMER_RUN:                                       // Count down and process PB3.
        LED_2 = 0;                                        // Keep LED2 off during the countdown.
        if (elapsed_value == 0 || (buttons & 0x4u) != 0u) // Detect completion or PB3.
        {
            request_state = TIMER_RUN_TRANSITION;             // Evaluate the exit condition.
            elapsed_before_pause = millis() - delay.start_ms; // Save the partial second.
            delay_start(CLEAR_DELAY_MS);                      // Start PB3 long-press detection.
        }
        if (delay_elapsed(&delay)) // Process each one-second interval.
        {
            LED_1 = !LED_1;        // Toggle the running indicator.
            if (elapsed_value > 0) // Prevent countdown underflow.
            {
                elapsed_value--; // Remove one remaining second.
            }
        }
        idle_low_power(); // Idle until the next interrupt.
        break;

    case TIMER_RUN_TRANSITION:  // Select alarm, pause, or clear.
        if (elapsed_value == 0) // Enter alarm when time expires.
        {
            request_state = TIMER_ALARM_ENTRY; // Prepare the alarm state.
        }
        else if ((buttons & 0x4u) != 0u) // Continue timing a held PB3.
        {
            if (delay_elapsed(&delay)) // Detect a PB3 long press.
            {
                request_state = TIMER_CLEAR_ENTRY; // Clear the active countdown.
            }
        }
        else // Treat PB3 release as a short press.
        {
            request_state = TIMER_PAUSE_ENTRY; // Pause the countdown.
        }
        break;

    case TIMER_PAUSE_ENTRY:                          // Announce the paused countdown.
        request_state = TIMER_PAUSE;                 // Continue into the paused state.
        selected_message = UART_COUNT_PAUSED_FORMAT; // Display the paused value.
        break;

    case TIMER_PAUSE:               // Wait for another PB3 press.
        if ((buttons & 0x4u) != 0u) // Detect PB3 while paused.
        {
            request_state = TIMER_PAUSE_TRANSITION; // Evaluate resume or clear.
            delay_start(CLEAR_DELAY_MS);            // Start PB3 long-press detection.
        }
        else // Remain paused without input.
        {
            Sleep(); // Sleep until an interrupt occurs.
        }
        break;

    case TIMER_PAUSE_TRANSITION:  // Select resume or clear.
        if ((buttons & 0x4) != 0) // Continue timing a held PB3.
        {
            if (delay_elapsed(&delay)) // Detect a PB3 long press.
            {
                request_state = TIMER_CLEAR_ENTRY; // Clear the paused countdown.
            }
        }
        else // Treat PB3 release as a short press.
        {
            selected_message = UART_COUNT_FORMAT; // Display the resumed value.
            request_state = TIMER_RUN_ENTRY;      // Resume the countdown.
        }
        break;

    case TIMER_CLEAR_ENTRY:          // Reset the timer and indicators.
        LED_1 = 0;                   // Turn off LED1.
        LED_2 = 0;                   // Turn off LED2.
        elapsed_value = 0;           // Clear remaining countdown time.
        preset_secs = 0;             // Clear preset seconds.
        preset_mins = 0;             // Clear preset minutes.
        request_state = TIMER_CLEAR; // Continue into the clear state.
        break;

    case TIMER_CLEAR: // Display and exit the cleared state.
        if (buttons)  // Return to setting while a button is active.
        {
            request_state = TIMER_IDLE_ENTRY; // Prepare the idle state.
        }
        selected_message = UART_CLEAR_FORMAT; // Display the clear confirmation.
        break;

    case TIMER_ALARM_ENTRY:                   // Prepare the completed alarm.
        LED_1 = 1;                            // Hold LED1 on during the alarm.
        LED_2 = 0;                            // Start LED2 from its off phase.
        delay_start(FAST_BLINK_MS);           // Start the rapid blink interval.
        selected_message = UART_ALARM_FORMAT; // Display the alarm message.
        request_state = TIMER_ALARM;          // Continue into the alarm state.
        break;

    case TIMER_ALARM:              // Blink LED2 until a setting button is used.
        LED_1 = 1;                 // Keep LED1 solidly on.
        if (delay_elapsed(&delay)) // Process each rapid blink interval.
        {
            LED_2 = !LED_2; // Toggle the alarm indicator.
        }
        if ((buttons & 0x3u) != 0u) // Detect PB1 or PB2.
        {
            request_state = TIMER_ALARM_TRANSITION; // Leave the alarm state.
            selected_message = UART_SET_FORMAT;     // Restore the preset display.
        }
        idle_low_power(); // Idle until the next interrupt.
        break;

    case TIMER_ALARM_TRANSITION:          // Disable the alarm indicators.
        LED_1 = 0;                        // Turn off LED1.
        LED_2 = 0;                        // Turn off LED2.
        request_state = TIMER_IDLE_ENTRY; // Return to preset setting.
        break;

    default:                              // Recover from an invalid state value.
        request_state = TIMER_INIT_ENTRY; // Restart state initialization.
        break;
    }

    elapsed_value = (uint16_t)clamp_preset_value((int16_t)elapsed_value); // Clamp the countdown range.
    switch (selected_message)                                             // Transmit the selected terminal message.
    {
    case UART_COUNT_FORMAT:                                                               // Print an active countdown update.
        print("%sCNT %02dm : %02ds", LINE_CLEAR, elapsed_value / 60, elapsed_value % 60); // Format minutes and seconds.
        break;
    case UART_ALARM_FORMAT:                           // Print the completed alarm message.
        print("%sFIN 00m : 00s - ALARM", LINE_CLEAR); // Show the final zero value.
        break;
    case UART_COUNT_PAUSED_FORMAT:                                                                 // Print a paused countdown update.
        print("%sCNT %02dm : %02ds {PAUSED}", LINE_CLEAR, elapsed_value / 60, elapsed_value % 60); // Mark the value as paused.
        break;
    case UART_INFO_FORMAT:                                   // Print the course and group information.
        print("%s2026 ENSF 460 L01 - Group 06", LINE_CLEAR); // Show the group identifier.
        Sleep();                                             // Sleep until the buttons change.
        break;
    case UART_SET_FORMAT:                                                   // Print the configured preset.
        print("%sSET %02dm : %02ds", LINE_CLEAR, preset_mins, preset_secs); // Format preset minutes and seconds.
        break;
    case UART_CLEAR_FORMAT:                   // Print the cleared timer message.
        print("%sCLR 00m : 00s", LINE_CLEAR); // Show the cleared zero value.
        break;
    default: // Skip output when no message is selected.
        break;
    }
}

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void) // Captures button changes from CN hardware.
{
    volatile uint16_t dummy_a = PORTA; // Read PORTA to clear the CN mismatch.
    volatile uint16_t dummy_b = PORTB; // Read PORTB to clear the CN mismatch.

    buttons = (uint16_t)_RB7 | ((uint16_t)_RB4 << 1) | ((uint16_t)_RA4 << 2); // Pack button pins into a bit mask.

    (void)dummy_a;     // Mark the mismatch read as intentionally unused.
    (void)dummy_b;     // Mark the mismatch read as intentionally unused.
    IFS1bits.CNIF = 0; // Clear the CN interrupt flag.
}
