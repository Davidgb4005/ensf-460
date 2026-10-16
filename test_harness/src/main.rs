mod event_log;
mod tui;
mod dgital_twin;
mod gpio;
//mod test_runner;
mod state;
mod uart;



use std::io::{self};

use crate::state::{TwinState,TimerStates};
use crate::gpio::{ControlGpio,TwinGpio};
use crate::uart::{UartReader};
use rppal::gpio::{Gpio};
use crossterm::event::{self, Event, KeyCode, KeyEventKind, KeyModifiers};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode};
use std::error::Error;
use std::sync::{Arc, RwLock, mpsc};
use std::{thread};
use std::time::{Duration};
use std::time::{SystemTime, UNIX_EPOCH};

const UART_PORT: &str = "/dev/ttyAMA0";
const UART_BAUD: u32 = 500000;

struct RawModeGuard;

impl RawModeGuard {
    fn new() -> io::Result<Self> {
        enable_raw_mode()?;
        Ok(Self)
    }
}

impl Drop for RawModeGuard {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
    }
}

fn now_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_millis()
}
fn main() -> Result<(), Box<dyn Error>> {
    let gpio = Gpio::new()?;

    let mut control_gpio = ControlGpio::new(&gpio)?;

    let twin_gpio = TwinGpio::new(&gpio)?;

    let shared_twin_state = Arc::new(RwLock::new(TwinState {
        current_state: TimerStates::Idle,
        prev_state: TimerStates::Idle,
        ..TwinState::default()
    }));

    let (twin_transition_tx, twin_transition_rx) = mpsc::channel();

    let (reset_tx, reset_rx) = mpsc::channel();

    let twin_state_writer = Arc::clone(&shared_twin_state);
    thread::spawn(move || {
        dgital_twin::run_digital_twin(twin_gpio, twin_state_writer, twin_transition_tx, reset_rx)
    });

    let mut port = serialport::new(UART_PORT, UART_BAUD)
        .timeout(Duration::from_millis(10))
        .open()?;
    let mut uart_reader = UartReader::default();
    let mut uart_state = TwinState::default();
    let mut uart_connected = false;
    let mut uart_error = None;
    let mut tui = tui::Tui::new(Duration::from_millis(100));
    let mut previous_gpio_outputs = control_gpio.output_state();
    let mut event_log = event_log::EventLogger::new("harness-events.log")?;
    let _raw_mode = RawModeGuard::new()?;

    let initial_twin_state = shared_twin_state
        .read()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clone();
    event_log.log(
        "SESSION_START",
        &uart_state,
        &initial_twin_state,
        previous_gpio_outputs,
    )?;

    let mut digital_start_time: Option<u128> = None;
    let mut digital_end_time: Option<u128> = None;
    let mut uart_start_time: Option<u128> = None;
    let mut uart_end_time: Option<u128> = None;
    let mut prev_digital_state = initial_twin_state.current_state;
    let mut prev_uart_state = uart_state.current_state;

    loop {
        if event::poll(Duration::ZERO)? {
            if let Event::Key(key) = event::read()? {
                if key.kind == KeyEventKind::Press {
                    if key.code == KeyCode::Char('c')
                        && key.modifiers.contains(KeyModifiers::CONTROL)
                    {
                        break;
                    }
                    if key.code == KeyCode::Char('r') {
                        reset_tx.send(())?;
                        control_gpio.trigger_hard_reset();
                        tui.record_reset();
                        let twin_snapshot = shared_twin_state
                            .read()
                            .unwrap_or_else(|poisoned| poisoned.into_inner())
                            .clone();
                        let gpio_outputs = control_gpio.output_state();
                        event_log.log(
                            "KEY_RESET: BCM 6 LOW -> HIGH",
                            &uart_state,
                            &twin_snapshot,
                            gpio_outputs,
                        )?;
                        previous_gpio_outputs.reset = true;
                    } else if key.code == KeyCode::Up && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_incrment_minute();
                    } else if key.code == KeyCode::Up {
                        control_gpio.trigger_incrment_second();
                    } else if key.code == KeyCode::Down
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_decrement_minute();
                    } else if key.code == KeyCode::Down {
                        control_gpio.trigger_decrement_second();
                    } else if key.code == KeyCode::Char('S')
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_reset();
                    } else if key.code == KeyCode::Char('s') {
                        control_gpio.trigger_start();
                    } else if key.code == KeyCode::Char('P')
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_clear();
                    } else if key.code == KeyCode::Char('p') {
                        control_gpio.trigger_pause();
                    }
                } 
            }
        }
        control_gpio.update();
        match uart_reader.poll(port.as_mut()) {
            Ok(Some(new_uart_state)) => {
                let mut transition_event = None;
                if uart_connected {
                    if new_uart_state.current_state != uart_state.current_state {
                        transition_event = Some(format!(
                            "UART_STATE: {:?} -> {:?}",
                            uart_state.current_state, new_uart_state.current_state
                        ));
                        tui.record_state_change(
                            "UART",
                            &uart_state.current_state,
                            &new_uart_state.current_state,
                        );
                    }
                } else if new_uart_state.current_state != new_uart_state.prev_state {
                    transition_event = Some(format!(
                        "UART_STATE: {:?} -> {:?}",
                        new_uart_state.prev_state, new_uart_state.current_state
                    ));
                    tui.record_state_change(
                        "UART",
                        &new_uart_state.prev_state,
                        &new_uart_state.current_state,
                    );
                }

                if let Some(event) = transition_event {
                    let twin_snapshot = shared_twin_state
                        .read()
                        .unwrap_or_else(|poisoned| poisoned.into_inner())
                        .clone();
                    event_log.log(
                        &event,
                        &new_uart_state,
                        &twin_snapshot,
                        control_gpio.output_state(),
                    )?;
                }
                uart_state = new_uart_state;
                uart_connected = true;
                uart_error = None;
            }
            Ok(None) => {}
            Err(error) => uart_error = Some(error.to_string()),
        }

        while let Ok(transition) = twin_transition_rx.try_recv() {
            tui.record_state_change("TWIN", &transition.prev_state, &transition.current_state);
            let event = format!(
                "TWIN_STATE: {:?} -> {:?}",
                transition.prev_state, transition.current_state
            );
            event_log.log(
                &event,
                &uart_state,
                &transition,
                control_gpio.output_state(),
            )?;
        }

        let twin_snapshot = shared_twin_state
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();

        let gpio_outputs = control_gpio.output_state();
        if gpio_outputs.pb_1 != previous_gpio_outputs.pb_1
            || gpio_outputs.pb_2 != previous_gpio_outputs.pb_2
            || gpio_outputs.pb_3 != previous_gpio_outputs.pb_3
        {
            tui.record_button_change(previous_gpio_outputs, gpio_outputs);
            let event = format!(
                "GPIO_BUTTONS: {:03b} -> {:03b}",
                dgital_twin::gpio_button_bits(previous_gpio_outputs),
                dgital_twin::gpio_button_bits(gpio_outputs)
            );
            event_log.log(&event, &uart_state, &twin_snapshot, gpio_outputs)?;
            previous_gpio_outputs.pb_1 = gpio_outputs.pb_1;
            previous_gpio_outputs.pb_2 = gpio_outputs.pb_2;
            previous_gpio_outputs.pb_3 = gpio_outputs.pb_3;
        }

        if twin_snapshot.current_state.clone() != prev_digital_state {
            match twin_snapshot.current_state {
                TimerStates::Run => {
                    digital_start_time = Some(now_ms());
                    digital_end_time = None;
                }
                TimerStates::Alarm => {
                    digital_end_time = Some(now_ms());
                    control_gpio.trigger_ping();
                }
                _ => {}
            }

            prev_digital_state = twin_snapshot.current_state;
        }

        if uart_state.current_state.clone() != prev_uart_state {
            match uart_state.current_state {
                TimerStates::RunEntry => {
                    uart_start_time = Some(now_ms());
                    uart_end_time = None;
                }
                TimerStates::AlarmEntry => uart_end_time = Some(now_ms()),
                _ => {}
            }

            prev_uart_state = uart_state.current_state;
        }

        if gpio_outputs.reset != previous_gpio_outputs.reset {
            let event = if gpio_outputs.reset {
                "GPIO_RESET: LOW -> HIGH"
            } else {
                "GPIO_RESET: HIGH -> LOW"
            };
            event_log.log(event, &uart_state, &twin_snapshot, gpio_outputs)?;
            previous_gpio_outputs.reset = gpio_outputs.reset;
        }
        let states_match = uart_connected.then_some(uart_state == twin_snapshot);

        tui.draw(
            &uart_state,
            &twin_snapshot,
            gpio_outputs,
            uart_connected,
            states_match,
            uart_error.as_deref(),
            uart_start_time,
            uart_end_time,
            digital_start_time,
            digital_end_time,
        )?;
        thread::sleep(Duration::from_millis(1));
    }

    Ok(())
}

