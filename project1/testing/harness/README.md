# Main Test Harness

The harness coordinates tests without implementing twin, TUI, or logger logic.
Both twins implement `TwinInterface` and publish immutable `TwinEvent` values to
the event sink supplied by the harness.

Test files use four commands:

```text
name Basic countdown

allow state 100ms
allow elapsed_seconds 1100ms
allow pb1 75ms

hold pb1 1s
wait 5s
hold pb1+pb2 1s
wait 10s
```

`hold` and `wait` are blocking schedule steps, so the duration is known when the
file is parsed. Bare step durations are seconds; bare `allow` durations are
milliseconds. Explicit `s` and `ms` suffixes are accepted everywhere.

Use `load_suite()` to parse files or expand directories in filename order, then
construct `TestHarness` with digital and hardware twins. The TUI, logger, and
power profiler are optional injected components.

Restart is built in: PB3 is held for four seconds, followed by PB1+PB2 for four
seconds. The harness then applies the configured restart mismatch limits and
requires both twins to match in their default state before rerunning the
interrupted test.
