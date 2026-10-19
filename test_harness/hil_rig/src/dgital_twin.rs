use crate::gpio::TwinGpio;
use crate::state::{GpioOutputState, LedState, SwitchState, TimerStates, TwinState};
use std::sync::{mpsc, Arc, RwLock};
use std::thread;
use std::time::{Duration, Instant};

const REPEAT_DELAY_MS: u32 = 200;
const CLEAR_DELAY_MS: u32 = 3000;
const COUNT_DELAY_MS: u32 = 1000;
struct EmulatorState {
    prev_buttons: u8,
    counter: u16,
    delay: u32,
    deadline: Instant,
    request_state: TimerStates,
}

impl EmulatorState {
    fn new() -> Self {
        Self {
            prev_buttons: 0,
            counter: 0,
            delay: REPEAT_DELAY_MS,
            deadline: Instant::now() + Duration::from_millis(REPEAT_DELAY_MS.into()),
            request_state: TimerStates::Idle,
        }
    }

    fn reset() -> Self {
        Self {
            request_state: TimerStates::Init,
            ..Self::new()
        }
    }
}

pub(crate) fn run_digital_twin(
    gpio: TwinGpio,
    shared_state: Arc<RwLock<TwinState>>,
    transition_tx: mpsc::Sender<TwinState>,
    reset_rx: mpsc::Receiver<()>,
) {
    let mut digital_state = TwinState {
        current_state: TimerStates::Idle,
        prev_state: TimerStates::Idle,
        ..TwinState::default()
    };
    let mut emulator_state = EmulatorState::new();

    loop {
        if reset_rx.try_recv().is_ok() {
            digital_state = TwinState {
                current_state: TimerStates::Init,
                prev_state: TimerStates::Idle,
                ..TwinState::default()
            };
            emulator_state = EmulatorState::reset();
        }

        let previous_state = digital_state.current_state.clone();
        cycle_digital_twin(&mut digital_state, &gpio, &mut emulator_state);

        if digital_state.current_state != previous_state
            && transition_tx.send(digital_state.clone()).is_err()
        {
            return;
        }

        *shared_state
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = digital_state.clone();
        thread::sleep(Duration::from_millis(1));
    }
}

fn cycle_digital_twin(
    digital_state: &mut TwinState,
    gpio: &TwinGpio,
    emulator_state: &mut EmulatorState,
) {
    let current_time = Instant::now();
    let buttons = gpio.buttons();
    digital_state.pb_1_state = switch_state(buttons & 0x1 != 0);
    digital_state.pb_2_state = switch_state(buttons & 0x2 != 0);
    digital_state.pb_3_state = switch_state(buttons & 0x4 != 0);

    if buttons != emulator_state.prev_buttons {
        emulator_state.prev_buttons = buttons;
        emulator_state.counter = 0;
        reset_deadline(current_time, emulator_state);
    }

    // The hardware requests PAUSE_TRANSITION and then sleeps while PB3 is
    // still held. It cannot enter the requested state until the release edge
    // wakes it, so hold the digital twin in PAUSE for the same interval.
    let pause_waiting_for_release = digital_state.current_state == TimerStates::Pause
        && emulator_state.request_state == TimerStates::PauseTransition
        && buttons & 0x4 != 0;

    if emulator_state.request_state != digital_state.current_state && !pause_waiting_for_release {
        digital_state.prev_state = digital_state.current_state.clone();
        digital_state.current_state = emulator_state.request_state.clone();
        reset_deadline(current_time, emulator_state);
        emulator_state.counter = 0;
    }

    match digital_state.current_state {
        TimerStates::InitEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::Init;
        }
        TimerStates::Init => emulator_state.request_state = TimerStates::InitTransition,
        TimerStates::InitTransition => emulator_state.request_state = TimerStates::InitToReset,
        TimerStates::InitToReset => emulator_state.request_state = TimerStates::Reset,
        TimerStates::IdleEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.delay = REPEAT_DELAY_MS;
            emulator_state.request_state = TimerStates::Idle;
        }
        TimerStates::Idle => match buttons {
            0b001 => adjust_preset(current_time, emulator_state, digital_state, 1, 5, true),
            0b010 => adjust_preset(current_time, emulator_state, digital_state, 60, 300, true),
            0b101 => adjust_preset(current_time, emulator_state, digital_state, 1, 5, false),
            0b110 => adjust_preset(current_time, emulator_state, digital_state, 60, 300, false),
            0b011 => {
                emulator_state.delay = CLEAR_DELAY_MS;
                emulator_state.request_state = TimerStates::IdleTransition;
            }
            _ => {}
        },
        TimerStates::IdleTransition => {
            if buttons == 0b011 {
                if current_time >= emulator_state.deadline {
                    emulator_state.request_state = TimerStates::IdleToReset;
                }
            } else if digital_state.preset_time > 0 {
                emulator_state.request_state = TimerStates::IdleToRun;
            } else {
                emulator_state.request_state = TimerStates::IdleEntry;
            }
        }
        TimerStates::IdleToRun => {
            digital_state.elapsed_time = digital_state.preset_time;
            emulator_state.request_state = TimerStates::RunEntry;
        }
        TimerStates::IdleToReset => emulator_state.request_state = TimerStates::ResetEntry,
        TimerStates::ResetEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::Reset;
        }
        TimerStates::Reset => {
            digital_state.preset_time = 0;
            emulator_state.request_state = TimerStates::ResetTransition;
        }
        TimerStates::ResetTransition | TimerStates::ResetToIdle => {
            emulator_state.request_state = TimerStates::IdleEntry;
        }
        TimerStates::RunEntry => {
            digital_state.led_1_state = LedState::Blink;
            digital_state.led_2_state = LedState::Off;
            emulator_state.delay = COUNT_DELAY_MS;
            emulator_state.request_state = TimerStates::Run;
        }
        TimerStates::Run => {
            if digital_state.elapsed_time == 0 || buttons & 0x4 != 0 {
                emulator_state.request_state = TimerStates::RunTransition;
                emulator_state.delay = CLEAR_DELAY_MS;
            }
            if current_time >= emulator_state.deadline {
                reset_deadline(current_time, emulator_state);
                digital_state.elapsed_time = digital_state.elapsed_time.saturating_sub(1);
            }
        }
        TimerStates::RunTransition => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            if digital_state.elapsed_time == 0 {
                emulator_state.request_state = TimerStates::RunToAlarm
            } else if buttons & 0x4 != 0 {
                if current_time >= emulator_state.deadline {
                    emulator_state.request_state = TimerStates::RunToClear;
                }
            } else {
                emulator_state.request_state = TimerStates::RunToPause;
            }
        }
        TimerStates::RunToAlarm => emulator_state.request_state = TimerStates::AlarmEntry,
        TimerStates::RunToPause => emulator_state.request_state = TimerStates::PauseEntry,
        TimerStates::RunToClear => emulator_state.request_state = TimerStates::ClearEntry,
        TimerStates::PauseEntry => {
            digital_state.led_1_state = LedState::Pause;
            digital_state.led_2_state = LedState::Off;
            emulator_state.delay = CLEAR_DELAY_MS;
            emulator_state.request_state = TimerStates::Pause;
        }
        TimerStates::Pause => {
            if buttons & 0x4 != 0 {
                emulator_state.request_state = TimerStates::PauseTransition;
            }
        }
        TimerStates::PauseTransition => {
            if buttons & 0x4 != 0 {
                if current_time >= emulator_state.deadline {
                    emulator_state.request_state = TimerStates::PauseToClear;
                }
            } else {
                emulator_state.request_state = TimerStates::PauseToRun;
            }
        }
        TimerStates::PauseToRun => emulator_state.request_state = TimerStates::RunEntry,
        TimerStates::PauseToClear => emulator_state.request_state = TimerStates::ClearEntry,
        TimerStates::ClearEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::Clear;
        }
        TimerStates::Clear => {
            digital_state.elapsed_time = 0;
            emulator_state.request_state = TimerStates::ClearTransition;
        }
        TimerStates::ClearTransition => emulator_state.request_state = TimerStates::ClearToIdle,
        TimerStates::ClearToIdle => emulator_state.request_state = TimerStates::IdleEntry,
        TimerStates::AlarmEntry => {
            digital_state.led_1_state = LedState::On;
            digital_state.led_2_state = LedState::Blink;
            emulator_state.request_state = TimerStates::Alarm;
        }
        TimerStates::Alarm => {
            if buttons & 0x3 != 0 {
                emulator_state.request_state = TimerStates::AlarmTransition;
            }
        }
        TimerStates::AlarmTransition => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::AlarmToIdle;
        }
        TimerStates::AlarmToIdle => emulator_state.request_state = TimerStates::IdleEntry,
    }
}

fn adjust_preset(
    now: Instant,
    emulator_state: &mut EmulatorState,
    digital_state: &mut TwinState,
    normal_step: u16,
    fast_step: u16,
    increase: bool,
) {
    if now < emulator_state.deadline {
        return;
    }
    reset_deadline(now, emulator_state);
    emulator_state.counter += 1;
    let step = if emulator_state.counter > 20 {
        fast_step
    } else {
        normal_step
    };
    digital_state.preset_time = if increase {
        digital_state.preset_time.saturating_add(step).min(3599)
    } else {
        digital_state.preset_time.saturating_sub(step)
    };
}

fn reset_deadline(now: Instant, emulator_state: &mut EmulatorState) {
    emulator_state.deadline = now + Duration::from_millis(emulator_state.delay.into());
}

fn switch_state(on: bool) -> SwitchState {
    if on {
        SwitchState::On
    } else {
        SwitchState::Off
    }
}

pub(crate) fn gpio_button_bits(state: GpioOutputState) -> u8 {
    (state.pb_1 as u8) | ((state.pb_2 as u8) << 1) | ((state.pb_3 as u8) << 2)
}
