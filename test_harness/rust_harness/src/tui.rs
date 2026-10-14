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
        let uart_current = format!("{:?}", uart.current_state);
        let twin_current = format!("{:?}", twin.current_state);
        let uart_previous = format!("{:?}", uart.prev_state);
        let twin_previous = format!("{:?}", twin.prev_state);
        let uart_led_1 = format!("{:?}", uart.led_1_state);
        let twin_led_1 = format!("{:?}", twin.led_1_state);
        let uart_led_2 = format!("{:?}", uart.led_2_state);
        let twin_led_2 = format!("{:?}", twin.led_2_state);
        let uart_pb_1 = format!("{:?}", uart.pb_1_state);
        let twin_pb_1 = format!("{:?}", twin.pb_1_state);
        let uart_pb_2 = format!("{:?}", uart.pb_2_state);
        let twin_pb_2 = format!("{:?}", twin.pb_2_state);
        let uart_pb_3 = format!("{:?}", uart.pb_3_state);
        let twin_pb_3 = format!("{:?}", twin.pb_3_state);

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
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n",
                "{:<18} {:<24} {:<24}\n",
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
            uart_current,
            twin_current,
            "Previous state",
            uart_previous,
            twin_previous,
            "LED 1",
            uart_led_1,
            twin_led_1,
            "LED 2",
            uart_led_2,
            twin_led_2,
            "Push button 1",
            uart_pb_1,
            twin_pb_1,
            "Push button 2",
            uart_pb_2,
            twin_pb_2,
            "Push button 3",
            uart_pb_3,
            twin_pb_3,
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
