

use rppal::gpio::{Gpio, InputPin, OutputPin};
use std::time::{Duration, Instant};
use crate::state::{GpioOutputState};

const TESTING_PIN_OUTPUT:u8 = 5;
const PUSHBUTTON_1_INPUT: u8 = 22;
const PUSHBUTTON_2_INPUT: u8 = 23;
const PUSHBUTTON_3_INPUT: u8 = 24;
const PUSHBUTTON_1_OUTPUT: u8 = 4;
const PUSHBUTTON_2_OUTPUT: u8 = 17;
const PUSHBUTTON_3_OUTPUT: u8 = 27;
const RESET_OUTPUT: u8 = 6;
const RESET_PULSE_MS: u64 = 3125;
const CLEAR_PULSE_MS: u64 = 3125;
const INCREMENT_PULSE_MS: u64 = 425;
const PULSE_MS: u64 = 100;

pub(crate) struct ControlGpio {
    push_button_outputs: [OutputPin; 3],
    reset_output: OutputPin,
    testing_output: OutputPin,
    reset_deadline: Option<Instant>,
}

impl ControlGpio {
    pub(crate) fn new(gpio: &Gpio) -> Result<Self, rppal::gpio::Error> {
        Ok(Self {
            push_button_outputs: [
                gpio.get(PUSHBUTTON_1_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_2_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_3_OUTPUT)?.into_output_low(),
            ],
            reset_output: gpio.get(RESET_OUTPUT)?.into_output_low(),
            testing_output: gpio.get(TESTING_PIN_OUTPUT)?.into_output_low(),
            reset_deadline: None,
        })
    }

    pub(crate) fn trigger_hard_reset(&mut self) {
        self.reset_output.set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(PULSE_MS));
    }

    pub(crate)fn trigger_incrment_second(&mut self) {
        self.push_button_outputs[0].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    pub(crate)fn trigger_incrment_minute(&mut self) {
        self.push_button_outputs[1].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    pub(crate)fn trigger_decrement_second(&mut self) {
        self.push_button_outputs[0].set_high();
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    pub(crate)fn trigger_decrement_minute(&mut self) {
        self.push_button_outputs[1].set_high();
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    pub(crate)fn trigger_start(&mut self) {
        self.push_button_outputs[0].set_high();
        self.push_button_outputs[1].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(PULSE_MS));
    }
    pub(crate)fn trigger_reset(&mut self) {
        self.push_button_outputs[0].set_high();
        self.push_button_outputs[1].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(RESET_PULSE_MS));
    }
    pub(crate)fn trigger_pause(&mut self) {
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(PULSE_MS));
    }
    pub(crate)fn trigger_clear(&mut self) {
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(CLEAR_PULSE_MS));
    }

    pub(crate)fn update(&mut self) {
        if self
            .reset_deadline
            .is_some_and(|deadline| Instant::now() >= deadline)
        {
            self.push_button_outputs[0].set_low();
            self.push_button_outputs[1].set_low();
            self.push_button_outputs[2].set_low();
            self.reset_output.set_low();
            self.reset_deadline = None;
        }
    }
    pub(crate)fn reset(&mut self) {
        self.push_button_outputs[0].set_low();
        self.push_button_outputs[1].set_low();
        self.push_button_outputs[2].set_low();
        self.reset_output.set_low();
        self.reset_deadline = None;
    }
    pub(crate)fn trigger_ping_high(&mut self) {
        self.testing_output.set_high();
    }
    pub(crate)fn trigger_ping_low(&mut self) {
        self.testing_output.set_low();
    }
    
    pub(crate)fn output_state(&self) -> GpioOutputState {
        GpioOutputState {
            pb_1: self.push_button_outputs[0].is_set_high(),
            pb_2: self.push_button_outputs[1].is_set_high(),
            pb_3: self.push_button_outputs[2].is_set_high(),
            reset: self.reset_output.is_set_high(),
        }
    }
}

pub(crate) struct TwinGpio {
    push_button_inputs: [InputPin; 3],
}

impl TwinGpio {
    pub(crate) fn new(gpio: &Gpio) -> Result<Self, rppal::gpio::Error> {
        Ok(Self {
            push_button_inputs: [
                gpio.get(PUSHBUTTON_1_INPUT)?.into_input_pulldown(),
                gpio.get(PUSHBUTTON_2_INPUT)?.into_input_pulldown(),
                gpio.get(PUSHBUTTON_3_INPUT)?.into_input_pulldown(),
            ],
        })
    }

    pub(crate) fn buttons(&self) -> u8 {
        (self.push_button_inputs[0].is_high() as u8)
            | ((self.push_button_inputs[1].is_high() as u8) << 1)
            | ((self.push_button_inputs[2].is_high() as u8) << 2)
    }
}