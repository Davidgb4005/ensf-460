

use std::fs::File;
use std::io::{self, BufRead, BufReader};
use std::path::Path;


#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Button {
    Pb1,
    Pb2,
    Pb3,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum TestCommand {
    AssertState(TimerStates),
    PressButton { button: Button, duration: Duration },
    Wait(Duration),
    Reset,
}

fn parse_test_file(path: impl AsRef<Path>) -> io::Result<Vec<TestCommand>> {
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    let mut commands = Vec::new();
    let test_name = reader.lines();
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