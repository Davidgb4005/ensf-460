#Initialization

All GPIO pin configuration is performed directly after entering the main() function and before entering the while(1) super loop. This initialization configures the required pins as either inputs for the three push buttons or an output for the LED.

#Super Loop and Program Logic

The main program logic is contained within the while(1) super loop. The three push buttons are read using three #define macros. Each macro reads the appropriate PORTx register, applies a bit mask to isolate the required input bit, and performs a Boolean comparison using != 0.

The resulting Boolean values are shifted into their corresponding bit positions to form the control value used by the switch statement. The switch statement then determines the required LED state and selects the appropriate delay value based on the current combination of push-button inputs.

PB3 PB2 PB1 <Switch Control Value>
 0   0   0  → 0
 0   0   1  → 1
 0   1   0  → 2
 0   1   1  → 3
 1   0   0  → 4
 ...
 1   1   1  → 7
 

#Variable Datatypes

variables are declared as uint16_t because the PIC24F16KA101 is a 16-bit microcontroller and its primary registers are 16 bits wide. Using fixed-width integer types also makes the intended size of each variable explicit.

The delay value is aquired via 2 16-bit intergers. A 16-bit unsigned integer has a maximum value of 65,535, which is not large enough to contain the loop counts required for the longer delays. For example, the 5-second delay requires a value of approximately 1675973. Therfore we nest to loops emulating a uint32_t without the performace cost of using non native register size. as seen below the ASM shows that for a uint32 based for loop is 37 instructions for i<1, where as a nested unint16 only takes 24 for i,k<1


void delay_16(){
	uint16_t ms_count = 0;
		while(ms_count<1){
			for(uint16_t i = 0; i<1;i++){
			}
			ms_count++;
		}
}
void delay_32(){	
		for(uint32_t i = 0; i<1;i++){
	}
}
000002ce <_delay_16>:
 2ce:	04 00 fa    	lnk       #0x4
 2d0:	00 00 eb    	clr.w     w0
 2d2:	00 0f 78    	mov.w     w0, [w14]
 2d4:	0b 00 37    	bra       0x2ec <.L2>
 //Outer Loop - K iterations
000002d6 <.L5>:
 2d6:	00 00 eb    	clr.w     w0
 2d8:	10 07 98    	mov.w     w0, [w14+2]
 2da:	03 00 37    	bra       0x2e2 <.L3>
 //Inner Loop - N iterations
000002dc <.L4>:
 2dc:	1e 00 90    	mov.w     [w14+2], w0
 2de:	00 00 e8    	inc.w     w0, w0
 2e0:	10 07 98    	mov.w     w0, [w14+2]
000002e2 <.L3>:
 2e2:	9e 00 90    	mov.w     [w14+2], w1
 2e4:	40 1f 20    	mov.w     #0x1f4, w0
 2e6:	80 8f 50    	sub.w     w1, w0, [w15]
 2e8:	f9 ff 36    	bra       LEU, 0x2dc <.L4>
 //Inner loop Ends
 2ea:	1e 0f e8    	inc.w     [w14], [w14]
000002ec <.L2>:
 2ec:	00 40 80    	mov.w     0x800, w0
 2ee:	9e 00 78    	mov.w     [w14], w1
 2f0:	80 8f 50    	sub.w     w1, w0, [w15]
 2f2:	f1 ff 39    	bra       NC, 0x2d6 <.L5>
 //Outer Loop Ends
 2f4:	00 80 fa    	ulnk      
 2f6:	00 00 06    	return    

Total Complexity N(7)+K(14)
assuming N of 10 and K of 10 where we call N, K times N(10)*10 + K(10) = 840 instructions

000002f8 <_delay_32>:
 2f8:	04 00 fa    	lnk       #0x4
 2fa:	60 00 b8    	mul.uu    w0, #0x0, w0
 2fc:	00 8f be    	mov.d     w0, [w14]
 2fe:	04 00 37    	bra       0x308 <.L7>
//Loop Starts
00000300 <.L8>: 
 300:	12 00 20    	mov.w     #0x1, w2
 302:	03 00 20    	mov.w     #0x0, w3
 304:	1e 0f 41    	add.w     w2, [w14], [w14]
 306:	5e 97 49    	addc.w    w3, [++w14], [w14--]
00000308 <.L7>:
 308:	00 40 80    	mov.w     0x800, w0
 30a:	80 00 eb    	clr.w     w1
 30c:	f2 14 20    	mov.w     #0x14f, w2
 30e:	02 89 b9    	mul.ss    w1, w2, w2
 310:	02 01 78    	mov.w     w2, w2
 312:	60 02 b9    	mul.su    w0, #0x0, w4
 314:	84 01 78    	mov.w     w4, w3
 316:	03 01 41    	add.w     w2, w3, w2
 318:	f3 14 20    	mov.w     #0x14f, w3
 31a:	03 00 b8    	mul.uu    w0, w3, w0
 31c:	01 01 41    	add.w     w2, w1, w2
 31e:	82 00 78    	mov.w     w2, w1
 320:	be 0f 50    	sub.w     w0, [w14++], [w15]
 322:	ae 8f 58    	subb.w    w1, [w14--], [w15]
 324:	ed ff 3e    	bra       GTU, 0x300 <.L8>
 //Loop Ends
 326:	00 80 fa    	ulnk      
 328:	00 00 06    	return   

 Total Complexity N(19)
assuming N of N(19)*100 = 1900 instructsions


#Blocking Delay and LED Response

The LED does reliably not turn on or off immediately after a push button is pressed or released because the current delay implementation is blocking. The program enters a for loop and remains inside that loop until the delay count has been reached.

While the program is executing the delay loop, the push-button inputs are not being checked. The CPU must first finish the current delay before execution can continue through the while(1) super loop. Once the delay finishes, the push buttons are read again, the switch statement determines the new state, and the LED state and delay value are updated.

Therefore, the maximum response time to a button change depends on where the program currently is within its delay. This behaviour is a consequence of using a simple blocking software delay rather than a non-blocking/interrupt based timer implementation.

