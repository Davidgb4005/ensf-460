use clap::Parser;
use rppal::gpio::{Gpio, InputPin, OutputPin};
use serialport::SerialPort;
use std::io::{self, Read};
use std::mem::discriminant;
use std::time::{Duration, Instant};

use crate::TimerStates::RunToPause;

const LED_1: u8 = 25;
const LED_2: u8 = 26;
const PUSHBUTTON_1_INPUT: u8 = 22;
const PUSHBUTTON_2_INPUT: u8 = 27;
const PUSHBUTTON_3_INPUT: u8 = 22;
const PUSHBUTTON_1_OUTPUT: u8 = 4;
const PUSHBUTTON_2_OUTPUT: u8 = 23;
const PUSHBUTTON_3_OUTPUT: u8 = 24;
const REPEAT_DELAY_MS: u32 = 400;
const CLEAR_DELAY_MS: u32 = 3000;
const COUNT_DELAY_MS: u32 = 1000;

#[derive(Default, Debug, Clone)]
enum LedState {
    #[default]
    Off,
    On,
    Blink,
    Pause,
}

#[derive(Default, Debug, Clone)]
enum SwitchState {
    On,
    #[default]
    Off,
}

#[derive(Default, Debug, Clone, PartialEq)]
enum TimerStates {
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
    PauseTransistion,
    PasueToRun,
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
            0 => Ok(Self::Init),
            1 => Ok(Self::Idle),
            2 => Ok(Self::Run),
            3 => Ok(Self::Pause),
            4 => Ok(Self::Alarm),
            _ => Err("Invalid Timer State"),
        }
    }
}
impl TryFrom<u8> for SwitchState {
    type Error = &'static str;
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Off),
            1 => Ok(Self::On),
            _ => Err("Invalid Timer State"),
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
            _ => Err("Invalid Blink State"),
        }
    }
}

#[derive(Default, Debug, Clone)]
struct TwinState {
    current_state: TimerStates,
    prev_state: TimerStates,
    led_1_state: LedState,
    led_2_state: LedState,
    pb_1_state: SwitchState,
    pb_2_state: SwitchState,
    pb_3_state: SwitchState,
    preset_time: u16,
    elapsed_time: u16,
}
struct HardwareGpio {
    led_inputs: [InputPin; 2],
    push_button_outputs: [OutputPin; 3],
    push_button_inputs: [InputPin; 3],
}

impl HardwareGpio {
    fn new() -> Result<Self, rppal::gpio::Error> {
        let gpio = Gpio::new()?;

        Ok(Self {
            led_inputs: [gpio.get(LED_1)?.into_input(), gpio.get(LED_2)?.into_input()],

            push_button_outputs: [
                gpio.get(PUSHBUTTON_1_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_2_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_3_OUTPUT)?.into_output_low(),
            ],

            push_button_inputs: [
                gpio.get(PUSHBUTTON_1_INPUT)?.into_input_pulldown(),
                gpio.get(PUSHBUTTON_2_INPUT)?.into_input_pulldown(),
                gpio.get(PUSHBUTTON_3_INPUT)?.into_input_pulldown(),
            ],
        })
    }
}

fn decode_packet(packet: &[u8; 9]) -> Option<TwinState> {
    if packet[0] != 0xAA || packet[1] != 0x55 {
        return None;
    }

    let checksum = packet[2..8]
        .iter()
        .fold(0u8, |sum, &byte| sum.wrapping_add(byte));

    if checksum != packet[8] {
        return None;
    }

    let flags = u16::from_le_bytes([packet[6], packet[7]]);

    Some(TwinState {
        current_state: TimerStates::try_from((flags & 0x07) as u8).ok()?,
        prev_state: TimerStates::try_from(((flags >> 3) & 0x07) as u8).ok()?,
        led_1_state: LedState::try_from(((flags >> 6) & 0x03) as u8).ok()?,
        led_2_state: LedState::try_from(((flags >> 8) & 0x03) as u8).ok()?,
        pb_1_state: SwitchState::try_from(((flags >> 10) & 0x01) as u8).ok()?,
        pb_2_state: SwitchState::try_from(((flags >> 11) & 0x01) as u8).ok()?,
        pb_3_state: SwitchState::try_from(((flags >> 12) & 0x01) as u8).ok()?,
        elapsed_time: u16::from_le_bytes([packet[2], packet[3]]),
        preset_time: u16::from_le_bytes([packet[4], packet[5]]),
    })
}

#[derive(Parser, Debug)]
#[command(version, about = "UART serial reader")]
struct Args {
    #[arg(short = 'D', long, default_value = "/dev/ttyUSB0")]
    port: String,

    #[arg(short = 'b', long, default_value_t = 9600)]
    baud: u32,

    #[arg(short = 't', long)]
    test: Option<String>,

    #[arg(short, long)]
    verbose: bool,
}

fn main() {
    let args = Args::parse();

    let digital_twin_thread = std::thread::spawn(|| {
        if let Err(error) = run_digital_twin() {
            eprintln!("Digital twin GPIO error: {error}");
        }
    });

    let path_to_tests = args.test;
    let baud = args.baud;
    let port_name = args.port;

    let mut port = serialport::new(port_name, baud)
        .timeout(Duration::from_millis(100))
        .open()
        .expect("Failed to open serial port");

    match path_to_tests {
        Some(path) => test_mode(path, port.as_mut()),
        None => manual_mode(port.as_mut()),
    }

    digital_twin_thread
        .join()
        .expect("Digital twin thread panicked");
}

fn test_mode(path: String, port: &mut dyn SerialPort) {
    println!("Test file: {path}");

    match uart_read(port) {
        Ok(Some(state)) => println!("{state:?}"),
        Ok(None) => println!("No packet received"),
        Err(e) => eprintln!("Serial error: {e}"),
    }
}

fn manual_mode(port: &mut dyn SerialPort) {
    loop {
        match uart_read(port) {
            Ok(Some(state)) => println!("{state:?}"),
            Ok(None) => {}
            Err(e) => {
                eprintln!("Serial error: {e}");
                break;
            }
        }
    }
}

fn uart_read(port: &mut dyn SerialPort) -> Result<Option<TwinState>, io::Error> {
    let mut buffer = [0u8; 128];
    let mut packet = Vec::new();
    let mut prev_byte = 0u8;

    loop {
        match port.read(&mut buffer) {
            Ok(n) => {
                for &byte in &buffer[..n] {
                    if packet.is_empty() {
                        if prev_byte == 0xAA && byte == 0x55 {
                            packet.extend_from_slice(&[0xAA, 0x55]);
                        }
                    } else {
                        packet.push(byte);

                        if packet.len() == 9 {
                            let data: [u8; 9] = packet.as_slice().try_into().unwrap();

                            return Ok(decode_packet(&data));
                        }
                    }

                    prev_byte = byte;
                }
            }

            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {}

            Err(e) => return Err(e),
        }
    }
}

struct EmulatorState {
    prev_buttons: u8,
    counter: u16,
    delay: u32,
    _delay_duration_obj: Duration,
    deadline: Instant,
    request_state: TimerStates,
    trigger_flag: u8,
}

impl EmulatorState {
    fn new() -> Self {
        let delay = REPEAT_DELAY_MS;
        let delay_duration = Duration::from_millis(delay.into());

        Self {
            prev_buttons: 0,
            counter: 0,
            delay,
            _delay_duration_obj: delay_duration,
            deadline: Instant::now() + delay_duration,
            request_state: TimerStates::Idle,
            trigger_flag: 0,
        }
    }
}

fn run_digital_twin() -> Result<(), rppal::gpio::Error> {
    let hardware_state = HardwareGpio::new()?;
    let mut digital_state = TwinState {
        current_state: TimerStates::Idle,
        prev_state: TimerStates::Idle,
        ..TwinState::default()
    };
    let mut emulator_state = EmulatorState::new();

    loop {
        cycle_digital_twin(&mut digital_state, &hardware_state, &mut emulator_state);

        std::thread::sleep(Duration::from_millis(1));
    }
}

fn cycle_digital_twin(
    digital_state: &mut TwinState,
    hardware_state: &HardwareGpio,
    emulator_state: &mut EmulatorState,
) {
    let current_time = Instant::now();
    let buttons = (hardware_state.push_button_inputs[0].is_high() as u8)
        | ((hardware_state.push_button_inputs[1].is_high() as u8) << 1)
        | ((hardware_state.push_button_inputs[2].is_high() as u8) << 2);

    if buttons != emulator_state.prev_buttons {
        emulator_state.prev_buttons = buttons;
        emulator_state.counter = 0;
        emulator_state.deadline = current_time + Duration::from_millis(emulator_state.delay.into());
    }

    if emulator_state.request_state != digital_state.current_state {
        println!(
            "State transition: {:?} -> {:?}",
            digital_state.current_state, emulator_state.request_state
        );
        digital_state.prev_state = digital_state.current_state.clone();
        digital_state.current_state = emulator_state.request_state.clone();
        emulator_state._delay_duration_obj = Duration::from_millis(emulator_state.delay.into());
        emulator_state.deadline = current_time + emulator_state._delay_duration_obj;
        emulator_state.counter = 0;
        emulator_state.trigger_flag = 0;
    }
    match digital_state.current_state {
        TimerStates::InitEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::Init;
        }
        TimerStates::Init => {
            emulator_state.request_state = TimerStates::InitTransition;
        }
        TimerStates::InitTransition => {
            emulator_state.request_state = TimerStates::InitToReset;
        }
        TimerStates::InitToReset => {
            emulator_state.request_state = TimerStates::Reset;
        }
        TimerStates::IdleEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.delay = REPEAT_DELAY_MS;
            emulator_state.request_state = TimerStates::Idle;
        }
        TimerStates::Idle => match buttons {
            0b001 => {
                if current_time >= emulator_state.deadline {
                    emulator_state.deadline =
                        current_time + Duration::from_millis(emulator_state.delay.into());
                    emulator_state.counter += 1;
                    digital_state.preset_time = digital_state
                        .preset_time
                        .saturating_add(if emulator_state.counter > 20 { 5 } else { 1 })
                        .min(3599);
                }
            }
            0b010 => {
                if current_time >= emulator_state.deadline {
                    emulator_state.deadline =
                        current_time + Duration::from_millis(emulator_state.delay.into());
                    emulator_state.counter += 1;
                    digital_state.preset_time = digital_state
                        .preset_time
                        .saturating_add(if emulator_state.counter > 20 { 300 } else { 60 })
                        .min(3599);
                }
            }
            0b101 => {
                if current_time >= emulator_state.deadline {
                    emulator_state.deadline =
                        current_time + Duration::from_millis(emulator_state.delay.into());
                    emulator_state.counter += 1;
                    digital_state.preset_time = digital_state
                        .preset_time
                        .saturating_sub(if emulator_state.counter > 20 { 5 } else { 1 });
                }
            }
            0b110 => {
                if current_time >= emulator_state.deadline {
                    emulator_state.deadline =
                        current_time + Duration::from_millis(emulator_state.delay.into());
                    emulator_state.counter += 1;
                    digital_state.preset_time = digital_state
                        .preset_time
                        .saturating_sub(if emulator_state.counter > 20 { 300 } else { 60 });
                }
            }
            0b011 => {
                emulator_state.delay = CLEAR_DELAY_MS;
                emulator_state.request_state = TimerStates::IdleTransition;
            }
            0b111 => {}
            _ => {}
        },
        TimerStates::IdleTransition => {
            if buttons ^ 0x3 == 0 {
                if current_time >= emulator_state.deadline {
                    emulator_state.request_state = TimerStates::IdleToReset;
                }
            } else {
                emulator_state.request_state = TimerStates::IdleToRun;
            }
        }
        TimerStates::IdleToRun => {
            digital_state.elapsed_time = digital_state.preset_time;
            emulator_state.request_state = TimerStates::RunEntry;
        }
        TimerStates::IdleToReset => {
            emulator_state.request_state = TimerStates::ResetEntry;
        }
        TimerStates::ResetEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::Reset;
        }
        TimerStates::Reset => {
            digital_state.preset_time = 0;
            emulator_state.request_state = TimerStates::ResetTransition
        }
        TimerStates::ResetTransition => {
            emulator_state.request_state = TimerStates::IdleEntry;
        }
        TimerStates::RunEntry => {
            digital_state.led_1_state = LedState::Blink;
            digital_state.led_2_state = LedState::Off;
            emulator_state.delay = COUNT_DELAY_MS;
            emulator_state.request_state = TimerStates::Run;
        }
        TimerStates::Run => {
            if digital_state.elapsed_time <= 0 || buttons & 0x4 > 0 {
                emulator_state.request_state = TimerStates::RunTransition;
            }

            if current_time >= emulator_state.deadline {
                emulator_state.deadline =
                    current_time + Duration::from_millis(emulator_state.delay.into());
                digital_state.elapsed_time = digital_state.elapsed_time.saturating_sub(1);
            }
        }
        TimerStates::RunTransition => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;

            if digital_state.elapsed_time == 0 {
                emulator_state.request_state = TimerStates::RunToAlarm;
            } else {
                emulator_state.request_state = TimerStates::RunToPause;
            }
        }
        TimerStates::RunToAlarm => {
            emulator_state.request_state = TimerStates::AlarmEntry;
        }
        TimerStates::RunToPause => {
            emulator_state.request_state = TimerStates::PauseEntry;
        }
        TimerStates::RunToClear => {
            emulator_state.request_state = TimerStates::ClearEntry;
        }
        TimerStates::PauseEntry => {
            digital_state.led_1_state = LedState::Pause;
            digital_state.led_2_state = LedState::Off;
            emulator_state.delay = CLEAR_DELAY_MS;
            emulator_state.request_state = TimerStates::Pause;
        }
        TimerStates::Pause => {
            if buttons & 0x4 > 0 {
                emulator_state.request_state = TimerStates::PauseTransistion;
            }
        }
        TimerStates::PauseTransistion => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;

            if buttons & 0x4 != 0 {
                if current_time >= emulator_state.deadline {
                    emulator_state.request_state = TimerStates::PauseToClear;
                }
            } else {
                emulator_state.request_state = TimerStates::PasueToRun;
            }
        }
        TimerStates::PasueToRun => {
            emulator_state.request_state = TimerStates::RunEntry;
        }
        TimerStates::PauseToClear => {
            emulator_state.request_state = TimerStates::ClearEntry;
        }
        TimerStates::ClearEntry => {
            digital_state.led_1_state = LedState::Off;
            digital_state.led_2_state = LedState::Off;
            emulator_state.request_state = TimerStates::Clear;
        }
        TimerStates::Clear => {
            digital_state.elapsed_time = 0;
            emulator_state.request_state = TimerStates::ClearTransition;
        }
        TimerStates::ClearTransition => {
            emulator_state.request_state = TimerStates::ClearToIdle;
        }
        TimerStates::ClearToIdle => {
            emulator_state.request_state = TimerStates::IdleEntry;
        }
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
        TimerStates::AlarmToIdle => {
            emulator_state.request_state = TimerStates::IdleEntry;
        }
        _ => {}
    }
}

fn reset_digital_twin(digital_state: &mut TwinState, hardware_state: &TwinState) {
    digital_state.clone_from(hardware_state);
}
