#include <xc.h>
#include "gpio.h"

typedef struct{
    volatile uint16_t *tris;
    volatile uint16_t *port;
    volatile uint16_t *lat;
    volatile uint16_t *odc;
}gpio_reg_t;

typedef struct{
    volatile uint16_t *cnen;
    volatile uint16_t *cnpu;
    volatile uint16_t *cnpd;
}cn_reg_t;

typedef struct{
    const gpio_reg_t *gpio_reg;
    uint16_t gpio_mask;
    mode_e mode;
    const cn_reg_t *cn_reg;
    uint16_t cn_mask;
}pin_t;

const static cn_reg_t CN_1 = {
    .cnen = &CNEN1,
    .cnpu = &CNPU1,
    .cnpd = &CNPD1
};
const static cn_reg_t CN_2= {
    .cnen = &CNEN2,
    .cnpu = &CNPU2,
    .cnpd = &CNPD2
};
const static gpio_reg_t PORT_A = {
    .tris = &TRISA,
    .port = &PORTA,
    .lat = &LATA,
    .odc = &ODCA
};
const static gpio_reg_t PORT_B = {
    .tris = &TRISB,
    .port = &PORTB,
    .lat = &LATB,
    .odc = &ODCB
};


static pin_t pin_array[19] = {
    {},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<5, .mode = UNINIT , .cn_reg = 0 , .cn_mask = 0},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<0, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<2},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<1, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<3},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<0, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<4},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<1, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<5},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<2, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<6},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<2, .mode = UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<14},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<3, .mode = UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<13},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<4, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<1},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<4, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<0},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<7, .mode = UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<7},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<8, .mode = UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<6},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<9, .mode = UNINIT , .cn_reg = &CN_2 , .cn_mask = 1<<7},
    {.gpio_reg = &PORT_A, .gpio_mask = 1<<6, .mode = UNINIT , .cn_reg = &CN_1 , .cn_mask = 1<<8},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<12, .mode = UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<14},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<13, .mode = UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<13},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<14, .mode = UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<12},
    {.gpio_reg = &PORT_B, .gpio_mask = 1<<15, .mode = UNINIT , .cn_reg =&CN_1 , .cn_mask = 1<<11},

};

uint16_t pinMode(uint16_t pin,mode_e mode){

    switch (mode){
        case UNINIT:
            return 0;
        case INPUT:
            *pin_array[pin].gpio_reg->tris |= pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpd &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpu &= ~pin_array[pin].cn_mask;
            pin_array[pin].mode = INPUT;
            return 1;
        case INPUT_PULLDOWN:
            *pin_array[pin].gpio_reg->tris |= pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpu &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpd |= pin_array[pin].cn_mask;
            pin_array[pin].mode = INPUT_PULLDOWN;
            return 1;
        case INPUT_PULLUP:
            *pin_array[pin].gpio_reg->tris |= pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpd &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpu |= pin_array[pin].cn_mask;
            pin_array[pin].mode = INPUT_PULLUP;
            return 1;
        case OUTPUT:
            *pin_array[pin].gpio_reg->tris &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].gpio_reg->odc &= ~pin_array[pin].gpio_mask;
            *pin_array[pin].cn_reg->cnpd &= ~pin_array[pin].cn_mask;
            *pin_array[pin].cn_reg->cnpu &= ~pin_array[pin].cn_mask;
            pin_array[pin].mode = OUTPUT;
    }
    return 0;
}


uint16_t digitalRead(uint16_t pin){
    return (*pin_array[pin].gpio_reg->port & pin_array[pin].gpio_mask) != 0;
}
void digitalWrite(uint16_t pin,uint16_t value){
    if (value){
        *pin_array[pin].gpio_reg->lat |= pin_array[pin].gpio_mask;
    }
    else{
        *pin_array[pin].gpio_reg->lat &= ~pin_array[pin].gpio_mask;
    }
}

