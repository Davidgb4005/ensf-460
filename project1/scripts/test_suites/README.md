# Digital Twin Command Tests

Run the normal automated tests with:

```sh
python3 scripts/run_test_suite.py
```

This runs `small/` and `large_functionality/`. The power-consumption command
sets are intentionally skipped because they contain longer waits. Run them with:

```sh
python3 scripts/run_test_suite.py --include-power
```

Every command file starts with a default reset preamble:

- hold `PB3` for 5 seconds
- hold `PB1+PB2` for 5 seconds
- verify `state=IDLE`, `preset=0`, and `elapsed=0`

For faster digital-only development runs, skip that preamble with:

```sh
python3 scripts/run_test_suite.py --no-startup-reset
```

To capture Nordic PPK2 current data while tests run:

```sh
python3 scripts/run_test_suite.py --power-profile --power-port /dev/ttyACM0
```

This writes one CSV per test into `scripts/power_logs/` by default. The TUI
harness supports the same profiler flags:

```sh
python3 scripts/testing_suite.py --power-profile --power-port /dev/ttyACM0 scripts/test_suites/large_power/02_short_interval_alarm.txt
```

By default, the profiler does not toggle PPK2 DUT power. If the PPK2 should
power the board during a capture, add `--power-control-dut`.

Command files support:

- `press <pb1|pb2|pb3|pb1+pb2> <seconds>`
- `wait <seconds>`
- `expect <state|preset|elapsed|pb1|pb2|pb3|led1_state|led1_output|led2_state|led2_output> <value>`
- `expect_range <preset|elapsed> <minimum> <maximum>`
- `wait_until <field> <value> <timeout_s>`

`press` starts a non-blocking press, so use `wait` when a test needs to wait
until the press has finished.
