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
#pragma config OSCIOFNC = ON            // CLKO Enable Configuration bit (CLKO output disabled; pin functions as port I/O)
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

#define PB1 ((PORTB & 1 << 7) != 0)             //Pin 17 -> control bit[0]
#define PB2 (((PORTB & 1 << 4) != 0) << 1)      //Pin 9  -> control bit[1]
#define PB3 (((PORTA & 1 << 4) != 0) << 2)      //Pin 10 -> control bit[2]
#define LED_TOGGLE LATB ^= 1 << 9
#define LED_ON LATB |= 1 << 9
#define LED_OFF LATB &= ~(1 << 9)
#define DELAY_750ms 251395                       //Provided C file uses fast FRC, use 108000/288000/720000 if on the FRCDIV(page 92))
#define DELAY_2000ms 670390
#define DELAY_5000ms 1675966

int main()
{
	AD1PCFG = 0xFFFF;
	TRISB = 1 << 7 | 1 << 4; //RB4,7 = INPUT
	TRISA = 1 << 4;			 //RA4 = INPUT
	CNPU1 = 0x0;
	CNPD1 = 1 << 0 | 1 << 1; //Enable PullDown on RB4,RA4
	CNPU2 = 0x0;
	CNPD2 = 1 << 7; 		 //Enable PullDown on RB7
	uint32_t blink_delay = 0;

	while (1)
	{
		for(volatile uint32_t i = 0; i<blink_delay;i++){
		}
		switch(PB1|PB2|PB3) // Bit 16 <15.xx|..|3.xx|2.PB3|1.PB2|0.PB1|> Bit 0 Control Register layout
		{
		case 1:							//PB1
			LED_TOGGLE;
			blink_delay = DELAY_750ms;
			break;
		case 2:							//PB2
			LED_TOGGLE;
			blink_delay = DELAY_2000ms;
			break;
		case 4:							//PB3
			LED_TOGGLE;
			blink_delay = DELAY_5000ms;
			break;
		case 3:
		case 5:
		case 6:
		case 7:	//Multi PB
			LED_ON;
			blink_delay = 0;
			break;
		case 0:
		default:					//No PB
			LED_OFF;
			blink_delay = 0;
			break;
		}
	}
	return 0;
}