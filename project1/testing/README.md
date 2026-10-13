# Hardware-in-the-loop test rig

Run the default sample suite with:

```bash
python3 testing/run.py --uart-port /dev/ttyUSB0
```

The TUI is enabled by default. Use `--headless` when running without an
interactive terminal:

```bash
python3 testing/run.py --headless --uart-port /dev/ttyUSB0
```

Supply a test file or directory after the options to replace the default
`testing/tests/` suite:

```bash
python3 testing/run.py --uart-port /dev/ttyUSB0 testing/tests/01_increment_seconds.txt
```

Each run creates a timestamped directory under `testing/output/` containing
the digital UART log, hardware UART log, and full state-event CSV. Add
`--power-profile` to capture one Nordic PPK2 CSV per test as well. Run
`python3 testing/run.py --help` for port, GPIO, output, and profiler options.

Runtime dependencies are listed per component:

```bash
python3 -m pip install -r testing/twin/requirements.txt
python3 -m pip install -r testing/power_profiler/requirements.txt  # optional
```
