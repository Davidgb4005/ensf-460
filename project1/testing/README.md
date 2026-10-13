# Hardware-in-the-loop test rig

Run the default sample suite with:

```bash
python3 testing/run.py
```

The TUI is enabled by default. Use `--headless` when running without an
interactive terminal:

```bash
python3 testing/run.py --headless
```

Supply a test file or directory after the options to replace the default
`testing/tests/` suite:

```bash
python3 testing/run.py testing/tests/01_increment_seconds.txt
```

Each run creates a timestamped directory under `testing/output/` containing
the digital UART log, hardware UART log, and full state-event CSV. Add
`--power-profile` to capture one Nordic PPK2 CSV per test as well. Run
`python3 testing/run.py --help` for port, GPIO, output, and profiler options.

## Raspberry Pi GPIO UART

The hardware twin uses `/dev/serial0` at 9600 baud by default. This is the
kernel hardware-UART alias for the UART routed to the Pi header; the harness
does not bit-bang or claim GPIO14/GPIO15 through `lgpio`.

Wire the target's TX to the Pi's RXD pin (GPIO15, physical pin 10) and connect
the grounds. If two-way communication is later required, connect the target's
RX to TXD (GPIO14, physical pin 8). The Pi UART is 3.3 V only.

The UART must be enabled and the Linux serial console disabled. On Raspberry Pi
OS this can be configured with `sudo raspi-config` under **Interface Options →
Serial Port**: disable the login shell and enable the serial hardware. Reboot
after changing it. The user running the test also needs access to
`/dev/serial0`, normally through the `dialout` group.

Confirm the alias before running:

```bash
ls -l /dev/serial0
```

Use `--uart-port` or `--baud-rate` only when the firmware is configured
differently.

Runtime dependencies are listed per component:

```bash
python3 -m pip install -r testing/twin/requirements.txt
python3 -m pip install -r testing/power_profiler/requirements.txt  # optional
```
