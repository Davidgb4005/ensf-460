mod event_log;
mod tui;

use std::fs::File;
use std::io::{self, BufRead, BufReader};
use std::path::Path;

use crossterm::event::{self, Event, KeyCode, KeyEventKind, KeyModifiers};
use std::time::{SystemTime, UNIX_EPOCH};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode};
use rppal::gpio::{Gpio, InputPin, OutputPin};
use serialport::{SerialPort, new};
use std::collections::VecDeque;
use std::error::Error;
use std::sync::{Arc, RwLock, mpsc};
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
const RESET_OUTPUT: u8 = 6;
const RESET_PULSE_MS: u64 = 3125;
const CLEAR_PULSE_MS: u64 = 3125;
const INCREMENT_PULSE_MS : u64 = 350;
const PULSE_MS: u64 = 100;
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

#[derive(Default, Debug,Copy, Clone, PartialEq, Eq)]
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

#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct GpioOutputState {
    pub(crate) pb_1: bool,
    pub(crate) pb_2: bool,
    pub(crate) pb_3: bool,
    pub(crate) reset: bool,
}

struct ControlGpio {
    push_button_outputs: [OutputPin; 3],
    reset_output: OutputPin,
    reset_deadline: Option<Instant>,
}

impl ControlGpio {
    fn new(gpio: &Gpio) -> Result<Self, rppal::gpio::Error> {
        Ok(Self {
            push_button_outputs: [
                gpio.get(PUSHBUTTON_1_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_2_OUTPUT)?.into_output_low(),
                gpio.get(PUSHBUTTON_3_OUTPUT)?.into_output_low(),
            ],
            reset_output: gpio.get(RESET_OUTPUT)?.into_output_low(),
            reset_deadline: None,
        })
    }

    fn trigger_hard_reset(&mut self) {
        self.reset_output.set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(PULSE_MS));
    }

    fn trigger_incrment_second(&mut self) {
        self.push_button_outputs[0].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    fn trigger_incrment_minute(&mut self) {
        self.push_button_outputs[1].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    fn trigger_decrement_second(&mut self) {
        self.push_button_outputs[0].set_high();
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    fn trigger_decrement_minute(&mut self) {
        self.push_button_outputs[1].set_high();
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(INCREMENT_PULSE_MS));
    }
    fn trigger_start(&mut self) {
        self.push_button_outputs[0].set_high();
        self.push_button_outputs[1].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(PULSE_MS));
    }
    fn trigger_reset(&mut self) {
        self.push_button_outputs[0].set_high();
        self.push_button_outputs[1].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(RESET_PULSE_MS));
    }
    fn trigger_pause(&mut self) {
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(PULSE_MS));
    }
    fn trigger_clear(&mut self) {
        self.push_button_outputs[2].set_high();
        self.reset_deadline = Some(Instant::now() + Duration::from_millis(CLEAR_PULSE_MS));
    }

    fn update(&mut self) {
        if self.reset_deadline.is_some_and(|deadline| Instant::now() >= deadline)
        {
            self.push_button_outputs[0].set_low();
            self.push_button_outputs[1].set_low();
            self.push_button_outputs[2].set_low();
            self.reset_output.set_low();
            self.reset_deadline = None;
        }
    }

    fn output_state(&self) -> GpioOutputState {
        GpioOutputState {
            pb_1: self.push_button_outputs[0].is_set_high(),
            pb_2: self.push_button_outputs[1].is_set_high(),
            pb_3: self.push_button_outputs[2].is_set_high(),
            reset: self.reset_output.is_set_high(),
        }
    }
}

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
        run_digital_twin(twin_gpio, twin_state_writer, twin_transition_tx, reset_rx)
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
                    if key.code == KeyCode::Char('r')
                    {
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
                    }
                    else if key.code == KeyCode::Up
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_incrment_minute();
                    }
                    else if key.code == KeyCode::Up
                    {
                        control_gpio.trigger_incrment_second();
                    }
                    else if key.code == KeyCode::Down
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_decrement_minute();
                    }
                    else if key.code == KeyCode::Down
                    {
                        control_gpio.trigger_decrement_second();
                    }
                    else if key.code == KeyCode::Char('S')
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_reset();
                    }
                    else if key.code == KeyCode::Char('s')
                    {
                        control_gpio.trigger_start();
                    }
                    else if key.code == KeyCode::Char('P')
                        && key.modifiers.contains(KeyModifiers::SHIFT)
                    {
                        control_gpio.trigger_clear();
                    }
                    else if key.code == KeyCode::Char('p')
                    {
                        control_gpio.trigger_pause();
                    }

                }
            }
            else{
            control_gpio.update();
            }
        }

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
                gpio_button_bits(previous_gpio_outputs),
                gpio_button_bits(gpio_outputs)
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
                TimerStates::Alarm => digital_end_time = Some(now_ms()),
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum TimerState {
    Idle,
    Run,
    Pause,
    Alarm,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Button {
    Pb1,
    Pb2,
    Pb3,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum TestCommand {
    AssertState(TimerState),
    PressButton { button: Button, duration: Duration },
    Wait(Duration),
    Reset,
}

fn parse_test_file(path: impl AsRef<Path>) -> io::Result<Vec<TestCommand>> {
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    let mut commands = Vec::new();

    for (line_index, line) in reader.lines().enumerate() {
        let line_number = line_index + 1;
        let line = line?;
        let line = line.trim();

        // Ignore empty lines and comments.
        if line.is_empty() || line.starts_with('#') {
            continue;
        }

        let command = parse_command(line).map_err(|message| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                format!("line {line_number}: {message}"),
            )
        })?;

        commands.push(command);
    }

    Ok(commands)
}

fn parse_command(line: &str) -> Result<TestCommand, String> {
    let mut tokens = line.split_whitespace();

    let command = tokens.next().ok_or_else(|| "missing command".to_owned())?;

    let parsed = match command {
        "assert" => {
            let state = parse_state(next_token(&mut tokens, "state")?)?;
            TestCommand::AssertState(state)
        }

        "pb" => {
            let button = parse_button(next_token(&mut tokens, "button number")?)?;

            let seconds = next_token(&mut tokens, "press duration")?
                .parse::<u64>()
                .map_err(|_| "button duration must be an integer".to_owned())?;

            TestCommand::PressButton {
                button,
                duration: Duration::from_secs(seconds),
            }
        }

        "wait" => {
            let seconds = next_token(&mut tokens, "wait duration")?
                .parse::<u64>()
                .map_err(|_| "wait duration must be an integer".to_owned())?;

            TestCommand::Wait(Duration::from_secs(seconds))
        }

        "reset" => TestCommand::Reset,

        unknown => {
            return Err(format!("unknown command `{unknown}`"));
        }
    };

    if let Some(extra) = tokens.next() {
        return Err(format!("unexpected argument `{extra}`"));
    }

    Ok(parsed)
}

fn next_token<'a>(
    tokens: &mut impl Iterator<Item = &'a str>,
    expected: &str,
) -> Result<&'a str, String> {
    tokens.next().ok_or_else(|| format!("missing {expected}"))
}

fn parse_state(token: &str) -> Result<TimerState, String> {
    match token {
        "idle" => Ok(TimerState::Idle),
        "run" => Ok(TimerState::Run),
        "pause" => Ok(TimerState::Pause),
        "alarm" => Ok(TimerState::Alarm),
        _ => Err(format!("unknown timer state `{token}`")),
    }
}

fn parse_button(token: &str) -> Result<Button, String> {
    match token {
        "1" => Ok(Button::Pb1),
        "2" => Ok(Button::Pb2),
        "3" => Ok(Button::Pb3),
        _ => Err(format!("invalid button `{token}`; expected 1, 2, or 3")),
    }
}

fn smain() -> Result<(), Box<dyn std::error::Error>> {
    let commands = parse_test_file("test.txt")?;

    for command in commands {
        println!("{command:?}");
    }

    Ok(())
}

fn consume_test_file(path: String, control_gpio: ControlGpio) {}

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

    fn reset() -> Self {
        Self {
            request_state: TimerStates::InitEntry,
            ..Self::new()
        }
    }
}

fn run_digital_twin(
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
            let previous_state = digital_state.current_state.clone();
            digital_state = TwinState {
                current_state: TimerStates::InitEntry,
                prev_state: previous_state,
                ..TwinState::default()
            };
            emulator_state = EmulatorState::reset();
            if transition_tx.send(digital_state.clone()).is_err() {
                return;
            }
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

fn gpio_button_bits(state: GpioOutputState) -> u8 {
    (state.pb_1 as u8) | ((state.pb_2 as u8) << 1) | ((state.pb_3 as u8) << 2)
}
