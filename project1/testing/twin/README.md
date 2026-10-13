# Twin adapters

This package contains the countdown-timer model, its configuration, and both
implementations of `testing.harness.TwinInterface`.

## Digital twin

`timer_model.py` contains the retained stopwatch state machine. `DigitalTwin`
wraps that model while its `main()` method still owns all timer behavior. The
adapter only:

- sets the three existing button objects to the requested levels;
- observes stopwatch, button, and LED fields;
- emits snapshots, state changes, and readable digital UART messages.

```python
from testing.twin import DEFAULT_CONFIG, DigitalTwin, build_digital_model

model = build_digital_model(DEFAULT_CONFIG)
digital_twin = DigitalTwin(model)
```

## Hardware twin

`HardwareTwin` directly owns its serial port and GPIO chip. There are no state
bit GPIO inputs. It:

- drives PB1, PB2, and PB3 through GPIO;
- samples the instantaneous LED1 and LED2 GPIO levels;
- receives timer state, previous state, times, buttons, and logical LED states
  from framed UART status packets;
- emits decoded UART messages and complete snapshots to the harness;
- closes the serial port and releases every GPIO line on `stop()`.

```python
hardware_twin = HardwareTwin(
    HardwareTwinConfig(
        uart_port="/dev/ttyAMA0",  # Pi 5 GPIO14/GPIO15 on Ubuntu
        baud_rate=9600,
        gpio_chip=0,
        pb1_drive_pin=4,
        pb2_drive_pin=17,
        pb3_drive_pin=27,
        led1_sample_pin=25,
        led2_sample_pin=26,
    )
)
```

The runner prefers the `/dev/serial0` Raspberry Pi hardware-UART alias and
falls back to `/dev/ttyAMA0` for Pi 5 Ubuntu systems where the alias is absent.
`/dev/ttyAMA10` is the Pi 5 debug-header UART, not GPIO14/GPIO15. The UART pins
are configured by Linux and are intentionally not claimed through `lgpio`;
`lgpio` is used only for the button-drive and LED-sample lines.

## UART frame

All multi-byte integers are little-endian. A complete frame is nine bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Marker `AA 55` |
| 2 | 2 | Elapsed seconds (`uint16_t`) |
| 4 | 2 | Preset seconds (`uint16_t`) |
| 6 | 2 | Packed flags (`uint16_t`) |
| 8 | 1 | Sum of payload bytes modulo 256 |

Packed flags:

| Bits | Field |
| --- | --- |
| 0–2 | Current timer state |
| 3–5 | Previous timer state |
| 6–8 | PB1, PB2, PB3 |
| 9–10 | LED1 logical state |
| 11–12 | LED2 logical state |
| 13–15 | Reserved; transmit as zero |

Timer codes match the firmware enum: `0=IDLE`, `1=RUN`, `2=PAUSE`, and
`3=ALARM`. The two-bit LED fields carry firmware LED modes: `0=BLINK`,
`1=PWM`, and `2=DIGITAL`. Value `3` is reserved.

The decoder accepts partial UART reads, finds the marker after unrelated bytes,
rejects bad checksums, and resumes searching for the next valid frame.
