
#ifndef _GPIO_H
#define _GPIO_H
#include <stdint.h>
#include "error.h"

typedef enum
{
	GPIO_UNINIT,
	GPIO_INPUT,
	GPIO_INPUT_PULLUP,
	GPIO_INPUT_PULLDOWN,
	GPIO_OUTPUT,
} gpio_mode_e;


uint16_t gpio_pin_mode(uint16_t pin, gpio_mode_e mode);
uint16_t gpio_digital_read(uint16_t pin);
error_e gpio_digital_write(uint16_t pin, uint16_t value);

#endif
