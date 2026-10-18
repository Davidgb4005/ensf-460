use std::fs::File;
use std::io::{self, BufRead, BufReader};
use std::path::Path;
use std::sync::{Arc, Mutex, RwLock};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use crate::gpio::ControlGpio;
use crate::state::{TimerStates, TwinState};
use crate::tcp::{open_connection, start_test, test_complete};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Command {
    Wait,
    Press,
    Assert,
    Reset,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ParsedCommand {
    command: Command,
    duration: Option<Duration>,
    push_button: Option<u8>,
    expected_state: Option<TimerStates>,
}

#[derive(Debug)]
struct TestDefinition {
    name: String,
    commands: Vec<ParsedCommand>,
}

impl ParsedCommand {
    fn parse(line: &str) -> Result<Self, String> {
        let mut tokens = line.split_whitespace();
        let command = tokens.next().ok_or_else(|| "missing command".to_owned())?;

        let parsed = match command {
            // `press 5 2` holds push button 2 for five seconds.
            "press" => {
                let seconds = parse_seconds(next_token(&mut tokens, "press duration")?)?;
                let push_button = next_token(&mut tokens, "push button number")?
                    .parse::<u8>()
                    .map_err(|_| "push button must be 1, 2, or 3".to_owned())?;
                if !(1..=3).contains(&push_button) {
                    return Err(format!(
                        "invalid push button {push_button}; expected 1, 2, or 3"
                    ));
                }
                Self {
                    command: Command::Press,
                    duration: Some(Duration::from_secs(seconds)),
                    push_button: Some(push_button),
                    expected_state: None,
                }
            }
            "wait" => {
                let seconds = parse_seconds(next_token(&mut tokens, "wait duration")?)?;
                Self {
                    command: Command::Wait,
                    duration: Some(Duration::from_secs(seconds)),
                    push_button: None,
                    expected_state: None,
                }
            }
            "assert" => Self {
                command: Command::Assert,
                duration: None,
                push_button: None,
                expected_state: Some(parse_state(next_token(&mut tokens, "timer state")?)?),
            },
            "reset" => Self {
                command: Command::Reset,
                duration: None,
                push_button: None,
                expected_state: None,
            },
            unknown => return Err(format!("unknown command `{unknown}`")),
        };

        if let Some(extra) = tokens.next() {
            return Err(format!("unexpected argument `{extra}`"));
        }
        Ok(parsed)
    }
}

fn next_token<'a>(
    tokens: &mut impl Iterator<Item = &'a str>,
    expected: &str,
) -> Result<&'a str, String> {
    tokens.next().ok_or_else(|| format!("missing {expected}"))
}

fn parse_seconds(token: &str) -> Result<u64, String> {
    token
        .parse::<u64>()
        .map_err(|_| "duration must be a non-negative integer in seconds".to_owned())
}

fn parse_state(token: &str) -> Result<TimerStates, String> {
    match token.to_ascii_lowercase().as_str() {
        "idle" => Ok(TimerStates::Idle),
        "run" => Ok(TimerStates::Run),
        "pause" => Ok(TimerStates::Pause),
        "alarm" => Ok(TimerStates::Alarm),
        "reset" => Ok(TimerStates::Reset),
        "clear" => Ok(TimerStates::Clear),
        _ => Err(format!(
            "unknown timer state `{token}`; expected idle, run, pause, alarm, reset, or clear"
        )),
    }
}

fn parse_test_file(path: impl AsRef<Path>) -> io::Result<TestDefinition> {
    let file = File::open(path)?;
    let mut lines = BufReader::new(file).lines().enumerate();
    let (_, name_line) = lines
        .next()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "test file is empty"))?;
    let name = name_line?.trim().to_owned();
    if name.is_empty() || name.starts_with('#') {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "line 1 must contain the test name",
        ));
    }

    let mut commands = Vec::new();
    for (line_index, line) in lines {
        let line_number = line_index + 1;
        let line = line?;
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let command = ParsedCommand::parse(line).map_err(|message| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                format!("line {line_number}: {message}"),
            )
        })?;
        commands.push(command);
    }
    Ok(TestDefinition { name, commands })
}

pub(crate) fn run_test(
    path: impl AsRef<Path>,
    control_gpio: Arc<Mutex<ControlGpio>>,
    uart_state: Arc<RwLock<TwinState>>,
) -> io::Result<JoinHandle<()>> {
    let test = parse_test_file(path)?;
    Ok(thread::spawn(move || {
        if let Err(error) = execute_test(&test, &control_gpio, &uart_state) {
            eprintln!("test `{}` failed: {error}", test.name);
        } else {
            println!("test `{}` completed", test.name);
        }
    }))
}

fn execute_test(
    test: &TestDefinition,
    control_gpio: &Arc<Mutex<ControlGpio>>,
    uart_state: &Arc<RwLock<TwinState>>,
) -> Result<(), String> {
    let mut tcp = open_connection().map_err(|error| format!("TCP connection failed: {error}"))?;
    start_test(&mut tcp, &test.name)
        .map_err(|error| format!("could not start power capture: {error}"))?;

    let result = execute_commands(&test.commands, control_gpio, uart_state);
    let complete_result = test_complete(&mut tcp)
        .map_err(|error| format!("could not complete power capture: {error}"));
    result.and(complete_result)
}

fn execute_commands(
    commands: &[ParsedCommand],
    control_gpio: &Arc<Mutex<ControlGpio>>,
    uart_state: &Arc<RwLock<TwinState>>,
) -> Result<(), String> {
    for command in commands {
        match command.command {
            Command::Wait => thread::sleep(command.duration.expect("wait duration was parsed")),
            Command::Press => {
                let button = command.push_button.expect("push button was parsed");
                set_button(control_gpio, button, true)?;
                thread::sleep(command.duration.expect("press duration was parsed"));
                set_button(control_gpio, button, false)?;
            }
            Command::Assert => {
                let expected = command.expected_state.expect("assert state was parsed");
                let actual = uart_state
                    .read()
                    .unwrap_or_else(|poisoned| poisoned.into_inner())
                    .current_state;
                if actual != expected {
                    return Err(format!(
                        "state assertion failed: expected {expected:?}, got {actual:?}"
                    ));
                }
            }
            Command::Reset => {
                control_gpio
                    .lock()
                    .unwrap_or_else(|poisoned| poisoned.into_inner())
                    .trigger_hard_reset();
            }
        }
    }
    Ok(())
}

fn set_button(
    control_gpio: &Arc<Mutex<ControlGpio>>,
    push_button: u8,
    pressed: bool,
) -> Result<(), String> {
    control_gpio
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .set_push_button(push_button, pressed)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn press_uses_duration_then_push_button_number() {
        let parsed = ParsedCommand::parse("press 5 2").unwrap();
        assert_eq!(parsed.command, Command::Press);
        assert_eq!(parsed.duration, Some(Duration::from_secs(5)));
        assert_eq!(parsed.push_button, Some(2));
    }

    #[test]
    fn rejects_gpio_numbers_as_buttons() {
        let error = ParsedCommand::parse("press 5 17").unwrap_err();
        assert!(error.contains("expected 1, 2, or 3"));
    }

    #[test]
    fn parses_wait_assert_and_reset() {
        assert_eq!(
            ParsedCommand::parse("wait 3").unwrap().duration,
            Some(Duration::from_secs(3))
        );
        assert_eq!(
            ParsedCommand::parse("assert run").unwrap().expected_state,
            Some(TimerStates::Run)
        );
        assert_eq!(
            ParsedCommand::parse("reset").unwrap().command,
            Command::Reset
        );
    }
}
