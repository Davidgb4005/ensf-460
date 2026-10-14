use crate::{GpioOutputState, TwinState};
use std::io::{self, Write};
use std::time::{Duration, Instant};

pub(crate) struct Tui {
    refresh_interval: Duration,
    next_draw: Instant,
}

impl Tui {
    pub(crate) fn new(refresh_interval: Duration) -> Self {
        Self {
            refresh_interval,
            next_draw: Instant::now(),
        }
    }

    pub(crate) fn draw(
        &mut self,
        uart: &TwinState,
        twin: &TwinState,
        gpio: GpioOutputState,
        uart_connected: bool,
        states_match: Option<bool>,
        uart_error: Option<&str>,
    ) -> io::Result<()> {
        let now = Instant::now();
        if now < self.next_draw {
            return Ok(());
        }
        self.next_draw = now + self.refresh_interval;

        let connection = if uart_connected {
            "receiving"
        } else {
            "waiting for packet"
        };
        let comparison = match states_match {
            Some(true) => "MATCH",
            Some(false) => "MISMATCH",
            None => "WAITING",
        };
        let error = uart_error.unwrap_or("none");

        let mut stdout = io::stdout().lock();
        write!(
            stdout,
            concat!(
                "\x1b[2J\x1b[H",
                "Timer test harness\n",
                "==================\n",
                "UART: {:<18} Comparison: {}\n",
                "UART error: {}\n\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24?} {:<24?}\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n\n",
                "GPIO drive outputs\n",
                "------------------\n",
                "PB1 (BCM 4):  {}\n",
                "PB2 (BCM 17): {}\n",
                "PB3 (BCM 27): {}\n",
            ),
            connection,
            comparison,
            error,
            "Field",
            "UART hardware",
            "Digital twin",
            "Current state",
            uart.current_state,
            twin.current_state,
            "Previous state",
            uart.prev_state,
            twin.prev_state,
            "LED 1",
            uart.led_1_state,
            twin.led_1_state,
            "LED 2",
            uart.led_2_state,
            twin.led_2_state,
            "Push button 1",
            uart.pb_1_state,
            twin.pb_1_state,
            "Push button 2",
            uart.pb_2_state,
            twin.pb_2_state,
            "Push button 3",
            uart.pb_3_state,
            twin.pb_3_state,
            "Preset seconds",
            uart.preset_time,
            twin.preset_time,
            "Elapsed seconds",
            uart.elapsed_time,
            twin.elapsed_time,
            level(gpio.pb_1),
            level(gpio.pb_2),
            level(gpio.pb_3),
        )?;
        stdout.flush()
    }
}

fn level(high: bool) -> &'static str {
    if high { "HIGH" } else { "LOW" }
}
