#include "xc.h"
#include "IOs.h"
#include "timer.h"

void IOinit(){
    AD1PCFG = 0xFFFF;                   // Configure analog-capable pins as digital I/O
	TRISB = 0x90;                       // Configure RB4 and RB7 as inputs; remaining PORTB pins as outputs
	TRISA = 0x10;                       // Configure RA4 as input; remaining PORTA pins as outputs

	CNPU1 = CNPU2 = 0x0;                // Disable internal pull-up resistors
	CNPD1 = 0x3;                        // Enable pull-down resistors for RB4 and RA4
	CNPD2 = 0x80;                       // Enable pull-down resistor for RB7
}

void IOcheck(){
    uint16_t control_bit = PB3|PB2|PB1;
    switch (control_bit)
    {
    case 1:
        delay_ms(250);
        break;
    case 2:                          // PB2 only: blink every 2000 ms
        delay_ms(1000);
        break;
    case 4:                          // PB3 only: blink every 5000 ms[]
        delay_ms(6000);
        break;
    case 3:                          // PB1 + PB2
    case 5:                          // PB1 + PB3
    case 6:                          // PB2 + PB3
    case 7:                          // All three buttons
        delay_ms(1);
        break;
    case 0:                          // No buttons pressed
    default:                        // Safe default for any unexpected control value
        delay_ms(0);
        LED_OFF_2;
        break;
    }
}