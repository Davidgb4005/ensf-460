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

Command files support:

- `press <pb1|pb2|pb3|pb1+pb2> <seconds>`
- `wait <seconds>`
- `expect <state|preset|elapsed|pb1|pb2|pb3|led1_state|led1_output|led2_state|led2_output> <value>`
- `expect_range <preset|elapsed> <minimum> <maximum>`
- `wait_until <field> <value> <timeout_s>`

`press` starts a non-blocking press, so use `wait` when a test needs to wait
until the press has finished.
