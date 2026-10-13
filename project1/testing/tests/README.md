# Simple HIL tests

These files are intentionally small examples for the rebuilt harness. Files in
this directory run in filename order when no paths are supplied to `run.py`.

```bash
python3 testing/run.py
```

Run one test without the TUI:

```bash
python3 testing/run.py --headless \
    testing/tests/01_increment_seconds.txt
```

The test format deliberately contains only fixed-duration actions and mismatch
tolerances:

```text
name Example
allow state 500ms
allow elapsed 1200ms
hold pb1 400ms
wait 600ms
```

The harness continuously compares every twin field. An `allow` line changes
how long that individual field may disagree before the test fails.
