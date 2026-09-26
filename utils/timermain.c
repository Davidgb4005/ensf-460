/*
 * File:   main.c
 * Author: BURGOIN, Dave
 *         Ogunjobi, Moyo
 *         Plourde, Jacob
 *
 * Created on: USE THE INFORMATION FROM THE HEADER MPLAB X IDE GENERATES FOR YOU
 */
#if 1
// FBS
#pragma config BWRP = OFF               // Table Write Protect Boot (Boot segment may be written)
#pragma config BSS = OFF                // Boot segment Protect (No boot program Flash segment)

// FGS
#pragma config GWRP = OFF               // General Segment Code Flash Write Protection bit (General segment may be written)
#pragma config GCP = OFF                // General Segment Code Flash Code Protection bit (No protection)

// FOSCSEL
#pragma config FNOSC = FRC              // Oscillator Select (Fast RC oscillator (FRC))
#pragma config IESO = OFF               // Internal External Switch Over bit (Internal External Switchover mode disabled (Two-Speed Start-up disabled))

// FOSC
#pragma config POSCMOD = NONE           // Primary Oscillator Configuration bits (Primary oscillator disabled)
#pragma config OSCIOFNC = OFF           // CLKO Enable Configuration bit (CLKO output disabled; pin functions as port I/O)
#pragma config POSCFREQ = HS            // Primary Oscillator Frequency Range Configuration bits (Primary oscillator/external clock input frequency greater than 8 MHz)
#pragma config SOSCSEL = SOSCHP         // SOSC Power Selection Configuration bits (Secondary oscillator configured for high-power operation)
#pragma config FCKSM = CSECMD           // Clock Switching and Monitor Selection (Clock switching is enabled, Fail-Safe Clock Monitor is disabled)

// FWDT
#pragma config WDTPS = PS32768          // Watchdog Timer Postscale Select bits (1:32,768)
#pragma config FWPSA = PR128            // WDT Prescaler (WDT prescaler ratio of 1:128)
#pragma config WINDIS = OFF             // Windowed Watchdog Timer Disable bit (Standard WDT selected; windowed WDT disabled)
#pragma config FWDTEN = OFF             // Watchdog Timer Enable bit (WDT disabled (control is placed on the SWDTEN bit))

// FPOR
#pragma config BOREN = BOR3             // Brown-out Reset Enable bits (Brown-out Reset enabled in hardware; SBOREN bit disabled)
#pragma config PWRTEN = ON              // Power-up Timer Enable bit (PWRT enabled)
#pragma config I2C1SEL = PRI            // Alternate I2C1 Pin Mapping bit (Default location for SCL1/SDA1 pins)
#pragma config BORV = V18               // Brown-out Reset Voltage bits (Brown-out Reset set to lowest voltage (1.8V))
#pragma config MCLRE = ON               // MCLR Pin Enable bit (MCLR pin enabled; RA5 input pin disabled)

// FICD
#pragma config ICS = PGx2               // ICD Pin Placement Select bits (PGC2/PGD2 are used for programming and debugging the device)

// FDS
#pragma config DSWDTPS = DSWDTPSF       // Deep Sleep Watchdog Timer Postscale Select bits (1:2,147,483,648 (25.7 Days))
#pragma config DSWDTOSC = LPRC          // DSWDT Reference Clock Select bit (DSWDT uses LPRC as reference clock)
#pragma config RTCOSC = SOSC            // RTCC Reference Clock Select bit (RTCC uses SOSC as reference clock)
#pragma config DSBOREN = ON             // Deep Sleep Zero-Power BOR Enable bit (Deep Sleep BOR enabled in Deep Sleep)
#pragma config DSWDTEN = ON             // Deep Sleep Watchdog Timer Enable bit (DSWDT enabled)
#endif
// #pragma config statements should precede project file includes.

#include <xc.h>
#include <p24F16KA101.h>
#include "stdint.h"
#include "timer.h"

#define PB1 ((PORTB & 0x80) >> 7)      // Read RB7 and place its value in control bit 0
#define PB2 ((PORTB & 0x10) >> 3)      // Read RB4 and place its value in control bit 1
#define PB3 ((PORTA & 0x10) >> 2)      // Read RA4 and place its value in control bit 2

#define OUTPUT_BITMASK 0x200            // Bitmask for LED output on RB9
#define LED_TOGGLE_1 LATB ^= OUTPUT_BITMASK // Toggle RB9 using XOR
#define LED_TOGGLE_2 LATA ^= 1<<6 // Toggle RB9 using XOR
#define LED_ON LATB |= OUTPUT_BITMASK     // Set RB9 high
#define LED_OFF LATB &= ~(OUTPUT_BITMASK) // Clear RB9 low
#define LED_OFF LATB &= ~(OUTPUT_BITMASK) // Clear RB9 low

#define DELAY_1ms 501                 // Calibrated loop count for approximately 1 ms
/**
 * Uses a busy-wait loop based on an assumed clock speed of 4 MHz to generate a
 * millisecond-resolution delay.
 */
timer_delay timer_1;
timer_delay timer_2;
timer_delay timer_3;
int main()
{
	timer2Init();
	AD1PCFG = 0xFFFF;                   // Configure analog-capable pins as digital I/O
	TRISB = 0x90;                       // Configure RB4 and RB7 as inputs; remaining PORTB pins as outputs
	TRISA = 0x10;                       // Configure RA4 as input; remaining PORTA pins as outputs

	CNPU1 = CNPU2 = 0x0;                // Disable internal pull-up resistors
	CNPD1 = 0x3;                        // Enable pull-down resistors for RB4 and RA4
	CNPD2 = 0x80;                       // Enable pull-down resistor for RB7
	timer_2.PT = 500;
	timer_2.IN = 1;
	timer_1.IN = 1;
	uint16_t toggle_on = 0;
    uint16_t prev_control_bit = 0;
	while (1)                           // Main superloop runs continuously
	{
		// Combine the three button inputs into a 3-bit control value: PB3|PB2|PB1
        switch (PB3|PB2|PB1)
        {
        case 1:  
            timer_1.PT = 250;
            if (!toggle_on){
                LED_ON;
                toggle_on = 1;
            }
            break;

        case 2:                          // PB2 only: blink every 2000 ms
            timer_1.PT = 1000;
            if (!toggle_on){
                LED_ON;
                toggle_on = 1;
            }
            break;

        case 4:                          // PB3 only: blink every 5000 ms
            timer_1.PT = 6000;
            if (!toggle_on){
                LED_ON;
                toggle_on = 1;
            }
            break;

        case 3:                          // PB1 + PB2
        case 5:                          // PB1 + PB3
        case 6:                          // PB2 + PB3
        case 7:                          // All three buttons
            timer_1.PT = 0;
            toggle_on = 0;
            break;
        case 0:                          // No buttons pressed
        default:                        // Safe default for any unexpected control value
            LED_OFF;
            toggle_on = 0;
            break;
        }

        TON_timer(&timer_1);
        timer_1.IN = 1;
		if(timer_1.Q){
			LED_TOGGLE_1;
			timer_1.IN = 0;
		}
        TON_timer(&timer_2);
        timer_2.IN = 1;
		if(timer_2.Q){
			LED_TOGGLE_2;
			timer_2.IN = 0;
		}


	}

	return 0;                            // Never reached because the superloop runs forever
}
