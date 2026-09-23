#import "@preview/codly:1.3.0": *
#import "@preview/codly-languages:0.1.1": *
#show: codly-init.with()
#codly(languages: codly-languages)

#set page( paper: "a4", numbering: "1 of 1", footer: context [ _ENSF 480,
  Shulich School of Engineering_ #h(1fr) #counter(page).display("1/1", both:
    true) ])

#set heading(numbering: "1.1")

#align(center)[
  #text(size: 14pt)[
    \ \ \ \ \ 
    *Names:* Dave Burgoin, Moyo Ogunjobi, Jacob Plourde \
    *Course Name:* Embedded Software and Hardware Systems \
    *Course Code:* ENSF 460 \
    *Assignment Number:* Assignment 1 Driver Project \
    *Date Submitted:* #datetime.today().display("[month repr:long] [day], [year]")
  ]
]

#pagebreak()

= Code Explanation

== Peripheral Initialization

All GPIO pin configuration is performed directly after entering the `main()`
function and before entering the `while(1)` super loop. This initialization
configures the required pins as either inputs for the three push buttons or an
output for the LED.

```c
int main()
{
  AD1PCFG = 0xFFFF;    // Configure analog-capable pins as digital I/O
  TRISB = 0x90;        // Configure RB4 and RB7 as inputs; remaining PORTB pins as outputs
  TRISA = 0x10;        // Configure RA4 as input; remaining PORTA pins as outputs

  CNPU1 = CNPU2 = 0x0; // Disable internal pull-up resistors
  CNPD1 = 0x3;         // Enable pull-down resistors for RB4 and RA4
  CNPD2 = 0x80;        // Enable pull-down resistor for RB7

  ...
```

== Super Loop and Program Logic

The main program logic is contained within the `while(1)` super loop. The three
push buttons are read using three `#define` macros. Each macro reads the
appropriate PORTx register, applies a bit mask to isolate the required input
bit, and performs a Boolean comparison using `!= 0`.

The resulting Boolean values are shifted into their corresponding bit positions
to form the control value used by the switch statement. The switch statement
then determines the required LED state and selects the appropriate delay value
based on the current combination of push-button inputs.

#table(
  columns: 4,
  table.header([*PB3*], [*PB2*], [*PB1*], [*\<Switch Control Value>*]),
  [0], [0], [0], [0],
  [0], [0], [1], [1],
  [0], [1], [0], [2],
  [0], [1], [1], [3],
  [1], [0], [0], [4],
  [1], [0], [1], [5],
  [1], [1], [0], [6],
  [1], [1], [1], [7],
)

== Datatype Choices

In general, integer variables are declared as `uint16_t` because the
PIC24F16KA101 is a 16-bit microcontroller, and so it has hardware native support
for 16-bit integers. Using fixed-width integer types also makes the intended
size of each variable explicit.

The value used to count `for` loop iterations in our `delay_ms()` function is
`uint32_t`, which internally uses two 16-bit registers to store and perform
arithmetic on. We must use a 32-bit integer for this because the maximum
`uint16_t` value is 65,535, which would only be able to delay for less than 500
ms. A 5-second delay, for example, requires a value of approximately 1,675,973,
which is far out of the `uint16_t` range.
