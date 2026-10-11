
#ifndef _GPIO_H
#define _GPIO_H
#include <stdint.h>

typedef enum
{
	UNINIT,
	INPUT,
	INPUT_PULLUP,
	INPUT_PULLDOWN,
	OUTPUT,
} mode_e;


uint16_t pinMode(uint16_t pin, mode_e mode);
uint16_t digitalRead(uint16_t pin);
void digitalWrite(uint16_t pin, uint16_t value);

#endif