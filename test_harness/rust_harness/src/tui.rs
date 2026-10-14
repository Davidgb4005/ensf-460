use crate::{GpioOutputState, TimerStates, TwinState};
use std::collections::VecDeque;
use std::io::{self, Write};
use std::time::{Duration, Instant};

pub(crate) struct Tui {
    refresh_interval: Duration,
    next_draw: Instant,
    started_at: Instant,
    state_log: VecDeque<StateLogEntry>,
}

struct StateLogEntry {
    timestamp: Duration,
    source: &'static str,
    previous: String,
    current: String,
}

impl Tui {
    pub(crate) fn new(refresh_interval: Duration) -> Self {
        Self {
            refresh_interval,
            next_draw: Instant::now(),
            started_at: Instant::now(),
            state_log: VecDeque::with_capacity(10),
        }
    }

    pub(crate) fn record_state_change(
        &mut self,
        source: &'static str,
        previous: &TimerStates,
        current: &TimerStates,
    ) {
        self.push_log_entry(source, format!("{previous:?}"), format!("{current:?}"));
    }

    pub(crate) fn record_button_change(
        &mut self,
        previous: GpioOutputState,
        current: GpioOutputState,
    ) {
        self.push_log_entry(
            "GPIO",
            format!("Buttons {:03b}", button_bits(previous)),
            format!("Buttons {:03b}", button_bits(current)),
        );
    }

    pub(crate) fn record_reset(&mut self) {
        self.push_log_entry("KEY", "Reset LOW".to_owned(), "Reset HIGH".to_owned());
    }

    fn push_log_entry(&mut self, source: &'static str, previous: String, current: String) {
        if self.state_log.len() == 10 {
            self.state_log.pop_front();
        }
        self.state_log.push_back(StateLogEntry {
            timestamp: self.started_at.elapsed(),
            source,
            previous,
            current,
        });
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
                "Timer test harness\r\n",
                "==================\r\n",
                "Press any key to reset; Ctrl-C exits\r\n",
                "UART: {:<18} Comparison: {}\r\n",
                "UART error: {}\r\n\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n",
                "{:<18} {:<24} {:<24}\r\n\r\n",
                "GPIO drive outputs\r\n",
                "------------------\r\n",
                "PB1 (BCM 4):  {}\r\n",
                "PB2 (BCM 17): {}\r\n",
                "PB3 (BCM 27): {}\r\n",
                "RESET (BCM 6): {}\r\n\r\n",
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
            level(gpio.reset),
        )?;

        write!(stdout, "State change log (last 10)\r\n")?;
        write!(stdout, "--------------------------\r\n")?;
        write!(
            stdout,
            "{:<13} {:<6} {}\r\n",
            "Timestamp", "Source", "Transition"
        )?;
        for entry in &self.state_log {
            write!(
                stdout,
                "{:<13} {:<6} {} -> {}\r\n",
                timestamp(entry.timestamp),
                entry.source,
                entry.previous,
                entry.current,
            )?;
        }
        stdout.flush()
    }
}

fn timestamp(elapsed: Duration) -> String {
    let total_seconds = elapsed.as_secs();
    let hours = total_seconds / 3600;
    let minutes = (total_seconds % 3600) / 60;
    let seconds = total_seconds % 60;
    format!(
        "{hours:02}:{minutes:02}:{seconds:02}.{:03}",
        elapsed.subsec_millis()
    )
}

fn level(high: bool) -> &'static str {
    if high { "HIGH" } else { "LOW" }
}

fn button_bits(state: GpioOutputState) -> u8 {
    (state.pb_1 as u8) | ((state.pb_2 as u8) << 1) | ((state.pb_3 as u8) << 2)
}
