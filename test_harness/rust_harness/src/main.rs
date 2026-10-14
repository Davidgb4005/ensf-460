mod tui;

use rppal::gpio::{Gpio, InputPin, OutputPin};
use serialport::SerialPort;
use std::collections::VecDeque;
use std::error::Error;
use std::io;
use std::sync::{Arc, RwLock};
use std::thread;
use std::time::{Duration, Instant};

const UART_PORT: &str = "/dev/ttyAMA0";
const UART_BAUD: u32 = 9600;
const PUSHBUTTON_1_INPUT: u8 = 22;
const PUSHBUTTON_2_INPUT: u8 = 23;
const PUSHBUTTON_3_INPUT: u8 = 24;
const PUSHBUTTON_1_OUTPUT: u8 = 4;
const PUSHBUTTON_2_OUTPUT: u8 = 17;
const PUSHBUTTON_3_OUTPUT: u8 = 27;
const REPEAT_DELAY_MS: u32 = 400;
const CLEAR_DELAY_MS: u32 = 3000;
const COUNT_DELAY_MS: u32 = 1000;

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) enum LedState {
    #[default]
    Off,
    On,
    Blink,
    Pause,
}

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) enum SwitchState {
    On,
    #[default]
    Off,
}

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) enum TimerStates {
    #[default]
    InitEntry,
    Init,
    InitTransition,
    InitToReset,
    IdleEntry,
    Idle,
    IdleTransition,
    IdleToRun,
    IdleToReset,
    ResetEntry,
    Reset,
    ResetTransition,
    ResetToIdle,
    RunEntry,
    Run,
    RunTransition,
    RunToAlarm,
    RunToPause,
    RunToClear,
    PauseEntry,
    Pause,
    PauseTransition,
    PauseToRun,
    PauseToClear,
    ClearEntry,
    Clear,
    ClearTransition,
    ClearToIdle,
    AlarmEntry,
    Alarm,
    AlarmTransition,
    AlarmToIdle,
}

impl TryFrom<u8> for TimerStates {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::InitEntry),
            1 => Ok(Self::Init),
            2 => Ok(Self::InitTransition),
            3 => Ok(Self::InitToReset),
            4 => Ok(Self::IdleEntry),
            5 => Ok(Self::Idle),
            6 => Ok(Self::IdleTransition),
            7 => Ok(Self::IdleToRun),
            8 => Ok(Self::IdleToReset),
            9 => Ok(Self::ResetEntry),
            10 => Ok(Self::Reset),
            11 => Ok(Self::ResetTransition),
            12 => Ok(Self::ResetToIdle),
            13 => Ok(Self::RunEntry),
            14 => Ok(Self::Run),
            15 => Ok(Self::RunTransition),
            16 => Ok(Self::RunToAlarm),
            17 => Ok(Self::RunToPause),
            18 => Ok(Self::RunToClear),
            19 => Ok(Self::PauseEntry),
            20 => Ok(Self::Pause),
            21 => Ok(Self::PauseTransition),
            22 => Ok(Self::PauseToRun),
            23 => Ok(Self::PauseToClear),
            24 => Ok(Self::ClearEntry),
            25 => Ok(Self::Clear),
            26 => Ok(Self::ClearTransition),
            27 => Ok(Self::ClearToIdle),
            28 => Ok(Self::AlarmEntry),
            29 => Ok(Self::Alarm),
            30 => Ok(Self::AlarmTransition),
            31 => Ok(Self::AlarmToIdle),
            _ => Err("invalid timer state"),
        }
    }
}

impl TryFrom<u8> for SwitchState {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Off),
            1 => Ok(Self::On),
            _ => Err("invalid switch state"),
        }
    }
}

impl TryFrom<u8> for LedState {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::On),
            1 => Ok(Self::Off),
            2 => Ok(Self::Blink),
            3 => Ok(Self::Pause),
            _ => Err("invalid LED state"),
        }
    }
}

#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub(crate) struct TwinState {
    pub(crate) current_state: TimerStates,
    pub(crate) prev_state: TimerStates,
    pub(crate) led_1_state: LedState,
    pub(crate) led_2_state: LedState,
    pub(crate) pb_1_state: SwitchState,
    pub(crate) pb_2_state: SwitchState,
    pub(crate) pb_3_state: SwitchState,
    pub(crate) preset_time: u16,
    pub(crate) elapsed_time: u16,
}

#[derive(Default, Debug, Clone, Copy)]
pub(crate) struct GpioOutputState {
    pub(crate) pb_1: bool,
    pub(crate) pb_2: bool,
    pub(crate) pb_3: bool,
}

struct ControlGpio {
    push_button_outputs: [OutputPin; 3],
}

impl ControlGpio {
    fn new(gpio: &Gpio) -> Result<Self, rppal::gpio::Error> {
        Ok(Self {
            push_button_outputs: [
                gpio.get(PUSHBUTTON_1_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_2_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_3_OUTPUT)?.into_output_low(),
            ],
        })
    }

    fn output_state(&self) -> GpioOutputState {
        GpioOutputState {
            pb_1: self.push_button_outputs[0].is_set_high(),
            pb_2: self.push_button_outputs[1].is_set_high(),
            pb_3: self.push_button_outputs[2].is_set_high(),
        }
    }
}

struct TwinGpio {
    push_button_inputs: [InputPin; 3],
}

impl TwinGpio {
    fn new(gpio: &Gpio) -> Result<Self, rppal::gpio::Error> {
        Ok(Self {
            push_button_inputs: [
                gpio.get(PUSHBUTTON_1_INPUT)?.into_input_pulldown(),
                gpio.get(PUSHBUTTON_2_INPUT)?.into_input_pulldown(),
                gpio.get(PUSHBUTTON_3_INPUT)?.into_input_pulldown(),
            ],
        })
    }

    fn buttons(&self) -> u8 {
        (self.push_button_inputs[0].is_high() as u8)
            | ((self.push_button_inputs[1].is_high() as u8) << 1)
            | ((self.push_button_inputs[2].is_high() as u8) << 2)
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let gpio = Gpio::new()?;
    let control_gpio = ControlGpio::new(&gpio)?;
    let twin_gpio = TwinGpio::new(&gpio)?;
    let shared_twin_state = Arc::new(RwLock::new(TwinState {
        current_state: TimerStates::Idle,
        prev_state: TimerStates::Idle,
        ..TwinState::default()
    }));

    let twin_state_writer = Arc::clone(&shared_twin_state);
    thread::spawn(move || run_digital_twin(twin_gpio, twin_state_writer));

    let mut port = serialport::new(UART_PORT, UART_BAUD)
        .timeout(Duration::from_millis(10))
        .open()?;
    let mut uart_reader = UartReader::default();
    let mut uart_state = TwinState::default();
    let mut uart_connected = false;
    let mut uart_error = None;
    let mut tui = tui::Tui::new(Duration::from_millis(100));

    loop {
        match uart_reader.poll(port.as_mut()) {
            Ok(Some(new_uart_state)) => {
                uart_state = new_uart_state;
                uart_connected = true;
                uart_error = None;
            }
            Ok(None) => {}
            Err(error) => uart_error = Some(error.to_string()),
        }

        let twin_snapshot = shared_twin_state
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();
        let gpio_outputs = control_gpio.output_state();
        let states_match = uart_connected.then_some(uart_state == twin_snapshot);

        tui.draw(
            &uart_state,
            &twin_snapshot,
            gpio_outputs,
            uart_connected,
            states_match,
            uart_error.as_deref(),
        )?;
        thread::sleep(Duration::from_millis(1));
    }
}

#[derive(Default)]
struct UartReader {
    packet: Vec<u8>,
    previous_byte: u8,
    decoded: VecDeque<TwinState>,
}

impl UartReader {
    fn poll(&mut self, port: &mut dyn SerialPort) -> io::Result<Option<TwinState>> {
        if let Some(state) = self.decoded.pop_front() {
            return Ok(Some(state));
        }

        let mut buffer = [0u8; 128];
        match port.read(&mut buffer) {
            Ok(byte_count) => {
                for &byte in &buffer[..byte_count] {
                    self.consume(byte);
                }
                Ok(self.decoded.pop_front())
            }
            Err(error) if error.kind() == io::ErrorKind::TimedOut => Ok(None),
            Err(error) => Err(error),
        }
    }

    fn consume(&mut self, byte: u8) {
        if self.packet.is_empty() {
            if self.previous_byte == 0xAA && byte == 0x55 {
                self.packet.extend_from_slice(&[0xAA, 0x55]);
            }
        } else {
            self.packet.push(byte);
            if self.packet.len() == 10 {
                let packet: [u8; 10] = self.packet.as_slice().try_into().unwrap();
                if let Some(state) = decode_packet(&packet) {
                    self.decoded.push_back(state);
                }
                self.packet.clear();
            }
        }
        self.previous_byte = byte;
    }
}

fn decode_packet(packet: &[u8; 10]) -> Option<TwinState> {
    if packet[0] != 0xAA || packet[1] != 0x55 {
        return None;
    }
    let checksum = packet[2..9]
        .iter()
        .fold(0u8, |sum, byte| sum.wrapping_add(*byte));
    if checksum != packet[9] {
        return None;
    }

    let gpio_flags = packet[6];
    let state_flags = u16::from_le_bytes([packet[7], packet[8]]);
    Some(TwinState {
        current_state: TimerStates::try_from((state_flags & 0x1f) as u8).ok()?,
        prev_state: TimerStates::try_from(((state_flags >> 5) & 0x1f) as u8).ok()?,
        led_1_state: LedState::try_from(gpio_flags & 0x03).ok()?,
        led_2_state: LedState::try_from((gpio_flags >> 2) & 0x03).ok()?,
        pb_1_state: SwitchState::try_from((gpio_flags >> 4) & 0x01).ok()?,
        pb_2_state: SwitchState::try_from((gpio_flags >> 5) & 0x01).ok()?,
        pb_3_state: SwitchState::try_from((gpio_flags >> 6) & 0x01).ok()?,
        elapsed_time: u16::from_le_bytes([packet[2], packet[3]]),
        preset_time: u16::from_le_bytes([packet[4], packet[5]]),
    })
}

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
}

fn run_digital_twin(gpio: TwinGpio, shared_state: Arc<RwLock<TwinState>>) {
    let mut digital_state = TwinState {
        current_state: TimerStates::Idle,
        prev_state: TimerStates::Idle,
        ..TwinState::default()
    };
    let mut emulator_state = EmulatorState::new();

    loop {
        cycle_digital_twin(&mut digital_state, &gpio, &mut emulator_state);
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

    if emulator_state.request_state != digital_state.current_state {
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
            } 
            else if digital_state.preset_time > 0{
                    emulator_state.request_state = TimerStates::IdleToRun;
            }
            else{
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
            }
            if current_time >= emulator_state.deadline {
                reset_deadline(current_time, emulator_state);
                digital_state.elapsed_time = digital_state.elapsed_time.saturating_sub(1);
            }
        }
        TimerStates::RunTransition => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = if digital_state.elapsed_time == 0 {
                TimerStates::RunToAlarm
            } else {
                TimerStates::RunToPause
            };
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
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
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
