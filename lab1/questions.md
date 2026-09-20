#Initialization

All GPIO pin configuration is performed directly after entering the main() function and before entering the while(1) super loop. This initialization configures the required pins as either inputs for the three push buttons or an output for the LED.

#Super Loop and Program Logic

The main program logic is contained within the while(1) super loop. The three push buttons are read using three #define macros. Each macro reads the appropriate PORTx register, applies a bit mask to isolate the required input bit, and performs a Boolean comparison using != 0.

The resulting Boolean values are shifted into their corresponding bit positions to form the control value used by the switch statement. The switch statement then determines the required LED state and selects the appropriate delay value based on the current combination of push-button inputs.

PB3 PB2 PB1 <Control Value>
 0   0   0  → 0
 0   0   1  → 1
 0   1   0  → 2
 0   1   1  → 3
 1   0   0  → 4
 ...
 1   1   1  → 7
 

#Variable Datatypes

variables are declared as uint16_t because the PIC24F16KA101 is a 16-bit microcontroller and its primary registers are 16 bits wide. Using fixed-width integer types also makes the intended size of each variable explicit.

The delay value is instead stored as a uint32_t. A 16-bit unsigned integer has a maximum value of 65,535, which is not large enough to contain the loop counts required for the longer delays. For example, the 5-second delay requires a value of approximately 1,675,973. A uint32_t can represent this value without overflowing.

#Blocking Delay and LED Response

The LED does reliably not turn on or off immediately after a push button is pressed or released because the current delay implementation is blocking. The program enters a for loop and remains inside that loop until the delay count has been reached.

While the program is executing the delay loop, the push-button inputs are not being checked. The CPU must first finish the current delay before execution can continue through the while(1) super loop. Once the delay finishes, the push buttons are read again, the switch statement determines the new state, and the LED state and delay value are updated.

Therefore, the maximum response time to a button change depends on where the program currently is within its delay. This behaviour is a consequence of using a simple blocking software delay rather than a non-blocking/interrupt based timer implementation.

