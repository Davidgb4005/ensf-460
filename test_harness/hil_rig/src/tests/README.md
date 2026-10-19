# Hardware test command lists

Run a command list from `test_harness/hil_rig` by passing its path to the
harness:

```sh
cargo run --bin rust_harness -- src/tests/function_tests/01_idle_edges.txt
cargo run --bin rust_harness -- src/tests/power_profiling_tests/01_idle_preset_fiddling.txt
```

The power logger TCP server must be running because every command list starts
and completes a power capture.

## Suites

- `function_tests/` checks state-machine behavior. These tests use `assert`
  after allowing transitions to settle and should fail if the hardware reports
  an unexpected stable state.
- `power_profiling_tests/` creates repeatable usage patterns for comparing
  energy consumption. These deliberately spend longer in each mode and avoid
  assertions that would interrupt a capture because of UART timing.

Button mappings used by the command lists:

- PB1: increment seconds; PB1 + PB3: decrement seconds
- PB2: increment minutes; PB2 + PB3: decrement minutes
- PB1 + PB2: start, or clear the idle preset when held for 3 seconds
- PB3: pause/resume when tapped; clear a running timer when held for 3 seconds
