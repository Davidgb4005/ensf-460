# Countdown Timer Test Runner

`run.py` is the top-level entry point for the digital-twin, physical-twin, TUI, automated tests, and optional Nordic PPK2 power captures.

Run commands from the project root:

```sh
python3 scripts/run.py [options]
```

## Live TUI

Start the TUI in manual mode. In this mode the Raspberry Pi GPIO inputs are sampled and the physical pushbutton outputs are not driven by the test harness:

```sh
python3 scripts/run.py --tui --manual
```

`--tui` is optional when no test file is supplied; the default is the manual TUI.

Run an automated command file while keeping the TUI visible:

```sh
python3 scripts/run.py --tui --tests scripts/test_suites/small/02_increment_seconds.txt
```

The automated TUI drives the configured pushbutton GPIO pins and displays test status, digital state, physical state, and differences between them.

Quit the TUI with `Ctrl+C`.

## Headless tests

Run the normal test groups (`small` and `large_functionality`):

```sh
python3 scripts/run.py --headless
```

Include the longer power-oriented scenarios:

```sh
python3 scripts/run.py --headless --include-power
```

Run one test, a directory, or several explicit paths:

```sh
python3 scripts/run.py --headless --tests scripts/test_suites/small/02_increment_seconds.txt
python3 scripts/run.py --headless --tests scripts/test_suites/small
python3 scripts/run.py --headless --tests \
    scripts/test_suites/small/02_increment_seconds.txt \
    scripts/test_suites/small/07_pause_resume.txt
```

When `--tests` is supplied, headless execution is selected automatically, so this is also valid:

```sh
python3 scripts/run.py --tests scripts/test_suites/small/02_increment_seconds.txt
```

By default, each test begins with a reset sequence: hold `PB3` for five seconds, then hold `PB1+PB2` for five seconds. Skip that sequence when the device is already in the desired initial state:

```sh
python3 scripts/run.py --headless --no-startup-reset \
    --tests scripts/test_suites/small/02_increment_seconds.txt
```

The runner prints one `PASS` or `FAIL` result per test followed by a summary.

## Command files

Command files are plain text files. Blank lines and text after `#` are ignored.

Supported commands:

```text
press <pb1|pb2|pb3|pb1+pb2> <seconds>
wait <seconds>
expect <field> <value>
expect_range <preset|elapsed> <minimum> <maximum>
wait_until <field> <value> <timeout_seconds>
note <text>
```

Fields accepted by `expect` and `wait_until` include:

```text
state preset elapsed
pb1 pb2 pb3
led1_state led1_output led2_state led2_output
```

Examples:

```text
# Set a one-second preset and start it
press pb1 1
wait 0.3
press pb1 0.1
wait_until state RUN 2
expect preset 1

# A short PB3 press pauses the timer
press pb3 0.1
wait_until state PAUSE 1
```

`press` schedules a non-blocking button press. Add a `wait` afterward when the test must wait for the press to finish.

## PPK2 power profiling

Capture one PPK2 CSV file per test:

```sh
python3 scripts/run.py --headless --power-profile \
    --power-port /dev/ttyACM0
```

Use a different output directory or measurement mode:

```sh
python3 scripts/run.py --headless --power-profile \
    --power-port /dev/ttyACM0 \
    --power-dir scripts/output/power \
    --power-mode source
```

The profiler does not control DUT power unless explicitly requested:

```sh
python3 scripts/run.py --headless --power-profile \
    --power-port /dev/ttyACM0 --power-control-dut
```

The PPK2 must be free from Nordic Power Profiler software while this runner is using `/dev/ttyACM0`. The user running the command must have permission to open the serial device, usually by being a member of the `dialout` group.

Power captures are written as CSV files under `scripts/output/power/` by default. To plot a capture:

```sh
cd scripts
PYTHONPATH=. python3 -m utils.power_csv_plotter \
    output/power/<capture>.csv
```

Add `--output output/power/<capture>.png` to save the graph instead of opening a window.

## Output files

Generated files are kept under `scripts/output/`:

```text
output/
  power/       PPK2 CSV captures and optional plots
  twin_events/ digital/physical twin event CSV log
```

The event log is written to `scripts/output/twin_events/twin_events.csv` by default.

## Configuration

Adjust pins and timing constants in [hardware/test_harness_config.py](hardware/test_harness_config.py). GPIO values are BCM GPIO numbers, not physical header pin numbers. Set a GPIO entry to `None` to disable that connection.

The Raspberry Pi GPIO backend must be available for physical-twin or manual-TUI operation. Headless digital-twin tests can run without Raspberry Pi GPIO hardware.

Useful direct diagnostics, run from `scripts/`:

```sh
python3 utils/gpio_diagnostics.py read
python3 utils/gpio_diagnostics.py pulse pb1 --duration 2
```

## Test directories

```text
test_suites/small/              Short focused behavior tests
test_suites/large_functionality/ Longer multi-step functional tests
test_suites/large_power/        Longer scenarios intended for power captures
```
