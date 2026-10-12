#include <xc.h>
#include "gpio.h"

typedef struct{
    volatile uint16_t *tris;
    volatile uint16_t *port;
    volatile uint16_t *lat;
    volatile uint16_t *odc;
} gpio_reg_s;

typedef struct{
    volatile uint16_t *cnen;
    volatile uint16_t *cnpu;
    volatile uint16_t *cnpd;
} cn_reg_s;

typedef struct{
    const gpio_reg_s *gpio_reg;
    uint16_t gpio_mask;
    gpio_mode_e mode;
    const cn_reg_s *cn_reg;
    uint16_t cn_mask;
} pin_s;

const static cn_reg_s CN_1 = {
    .cnen = &CNEN1,
    .cnpu = &CNPU1,
    .cnpd = &CNPD1
};
const static cn_reg_s CN_2= {
    .cnen = &CNEN2,
    .cnpu = &CNPU2,
    .cnpd = &CNPD2
};
const static gpio_reg_s PORT_A = {
    .tris = &TRISA,
    .port = &PORTA,
    .lat = &LATA,
    .odc = &ODCA
};
const static gpio_reg_s PORT_B = {
    .tris = &TRISB,
    .port = &PORTB,
    .lat = &LATB,
    .odc = &ODCB
};


static pin_s pin_array[19] = {
    {},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<5, .mode = GPIO_UNINIT , .cn_reg = 0 , .cn_mask = 0},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<0, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<2},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<1, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<3},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<0, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<4},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<1, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<5},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<2, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<6},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<2, .mode = GPIO_UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<14},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<3, .mode = GPIO_UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<13},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<4, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<1},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<4, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<0},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<7, .mode = GPIO_UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<7},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<8, .mode = GPIO_UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<6},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<9, .mode = GPIO_UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<7},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<6, .mode = GPIO_UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<8},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<12, .mode = GPIO_UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<14},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<13, .mode = GPIO_UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<13},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<14, .mode = GPIO_UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<12},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<15, .mode = GPIO_UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<11},

};

uint16_t gpio_pin_mode(uint16_t pin, gpio_mode_e mode){

    switch (mode){
        case GPIO_UNINIT:
            return ERROR_NONE;
        case GPIO_INPUT:
            *pin_array[pin].gpio_reg->tris |= pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpd &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpu &= ~pin_array[pin].cn_mask;
            pin_array[pin].mode = GPIO_INPUT;
            return ERROR_NONE;
        case GPIO_INPUT_PULLDOWN:
            *pin_array[pin].gpio_reg->tris |= pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpu &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpd |= pin_array[pin].cn_mask;
            pin_array[pin].mode = GPIO_INPUT_PULLDOWN;
            return ERROR_NONE;
        case GPIO_INPUT_PULLUP:
            *pin_array[pin].gpio_reg->tris |= pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpd &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpu |= pin_array[pin].cn_mask;
            pin_array[pin].mode = GPIO_INPUT_PULLUP;
            return ERROR_NONE;
        case GPIO_OUTPUT:
            *pin_array[pin].gpio_reg->tris &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpd &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpu &= ~pin_array[pin].cn_mask;
            pin_array[pin].mode = GPIO_OUTPUT;
            return ERROR_NONE;
    }
    return ERROR_UNSUPPORTED_MODE;
}


uint16_t gpio_digital_read(uint16_t pin){
    return (*pin_array[pin].gpio_reg->port & pin_array[pin].gpio_mask) != 0;
}
error_e gpio_digital_write(uint16_t pin, uint16_t value){
    if (value){
        *pin_array[pin].gpio_reg->lat |= pin_array[pin].gpio_mask;
    }
    else{
        *pin_array[pin].gpio_reg->lat &= ~pin_array[pin].gpio_mask;
    }
    return ERROR_NONE;
}
