#import "@preview/codly:1.3.0": *
#import "@preview/codly-languages:0.1.1": *
#show: codly-init.with()
#codly(languages: codly-languages)

#set page( paper: "a4", numbering: "1 of 1", footer: context [ _ENSF 460,
  Shulich School of Engineering_ #h(1fr) #counter(page).display("1/1", both:
    true) ])

#set heading(numbering: "1.1")

#set text(font: "IBM Plex Sans")

#align(center)[
  #text(size: 14pt)[
    \ \ \ \ \ 
    *Names:* Dave Burgoin, Moyo Ogunjobi, Jacob Plourde \
    *Group \#*: 6 \
    *Course:* ENSF 460 - Embedded Software and Hardware Systems \
    *Assignment Number:* Assignment 2 Driver Project \
    *Date Submitted:* #datetime.today().display("[month repr:long] [day], [year]")
  ]
]

#pagebreak()

= Peripheral Initialization

All IO pin initialization is handled in the `IOinit()` function, which is called
in our `main()` function before the `while(1)` superloop begins.

```C
void IOinit(){
  AD1PCFG = 0xFFFF;     // Configure analog-capable pins as digital I/O
  TRISB = 0x90;         // Configure RB4 and RB7 as inputs; remaining PORTB pins as outputs
  TRISA = 0x10;         // Configure RA4 as input; remaining PORTA pins as outputs

  CNPU1 = CNPU2 = 0x0;  // Disable internal pull-up resistors
  CNPD1 = 0x3;          // Enable pull-down resistors for RB4 and RA4
  CNPD2 = 0x80;         // Enable pull-down resistor for RB7
}
```

= Superloop

Our `while(1)` superloop singularly contains a call to `IOcheck()`. `IOcheck()`
is responsible for reading the state of each push-button, and uses a switch
statement to determine the output for the user-controlled LED. The structure for
this is much like the structure for driver project 1, with the exception that
the `delay_ms()` function in this version is implemented with a timer that idles
the microcontroller while is it delayed. To ensure that the other LED constantly
blinks at 500 ms intervals, it runs on a separate timer, where it is toggled on
each timer interrupt.

```C
#define PB1 ((PORTB & 0x80) >> 7)  // Read RB7 into control bit 0
#define PB2 ((PORTB & 0x10) >> 3)  // Read RB4 into control bit 1
#define PB3 ((PORTA & 0x10) >> 2)  // Read RA4 into control bit 2
#define LED1_MASK 0x0200           // RB9
#define LED2_MASK 0x0040           // RA6
#define LED_TOGGLE_1 (LATB ^= LED1_MASK)
#define LED_ON_1     (LATB |= LED1_MASK)
#define LED_OFF_1    (LATB &= ~LED1_MASK)
#define LED_TOGGLE_2 (LATA ^= LED2_MASK)
#define LED_ON_2     (LATA |= LED2_MASK)
#define LED_OFF_2    (LATA &= ~LED2_MASK)

void IOcheck(){
  uint16_t control_bit = PB3|PB2|PB1;

  switch (control_bit)
  {
    case 1:
      delay_ms(250);
      break;
    case 2:            // PB2 only: blink every 1000 ms
      delay_ms(1000);
      break;
    case 4:            // PB3 only: blink every 6000 ms
      delay_ms(6000);
      break;
    case 3:            // PB1 + PB2
    case 5:            // PB1 + PB3
    case 6:            // PB2 + PB3
    case 7:            // All three buttons
      delay_ms(1);
      break;
    case 0:            // No buttons pressed
    default:           // Safe default for any unexpected control value
      LED_OFF_2;
      break;
  }
}
```

= Timer Logic

== Prescaler Choices

= Power-Saving Features

== `Idle()` During `delay_ms()`

While the `delay_ms()` function is blocking until the specified number of
milliseconds have passed, it does not need to be wasting clock cycles just
iterating a `for` loop, as it was done in driver project 1. This implementation
of `delay_ms()` will call `Idle()` after it has configured its timer with the
appropriate prescaler and period values, and the microcontroller will free this
idle state when the timer interrupt triggers. Because we have two separate
timers to control each of the LEDs, this `Idle()` is continuously be called
whenever it is awoken until the correct ISR is called and sets the `isr_flag`
flag:

```C
while(!isr_flag){
  Idle();
}
```

This means that during a delay, the CPU will be idle for a significant majority
of the time, thereby saving power.
