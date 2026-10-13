import argparse
import time

from hardware.config import CONFIG, GpioState
from hardware.physical_twin import GpioInput, GpioOutput, configure_gpio_backend


def _state(value):
    return "1" if value == GpioState.ON else "0"


def _build_inputs(config):
    gpio = config.gpio
    return (
        ("PB1 sample", gpio.pb1_sample_pin, GpioInput(gpio.pb1_sample_pin, gpio.active_high)),
        ("PB2 sample", gpio.pb2_sample_pin, GpioInput(gpio.pb2_sample_pin, gpio.active_high)),
        ("PB3 sample", gpio.pb3_sample_pin, GpioInput(gpio.pb3_sample_pin, gpio.active_high)),
        ("LED1 sample", gpio.led1_sample_pin, GpioInput(gpio.led1_sample_pin, gpio.active_high)),
        ("LED2 sample", gpio.led2_sample_pin, GpioInput(gpio.led2_sample_pin, gpio.active_high)),
        ("State bit0", gpio.state_bit0_pin, GpioInput(gpio.state_bit0_pin, gpio.active_high)),
        ("State bit1", gpio.state_bit1_pin, GpioInput(gpio.state_bit1_pin, gpio.active_high)),
    )


def _build_outputs(config):
    gpio = config.gpio
    return {
        "pb1": GpioOutput(gpio.pb1_drive_pin, gpio.active_high),
        "pb2": GpioOutput(gpio.pb2_drive_pin, gpio.active_high),
        "pb3": GpioOutput(gpio.pb3_drive_pin, gpio.active_high),
    }


def read_loop(args):
    configure_gpio_backend(CONFIG.gpio.gpio_chip)
    inputs = _build_inputs(CONFIG)
    try:
        while True:
            line = "  ".join(
                f"{name} GPIO{pin}={_state(gpio_input.read())}"
                for name, pin, gpio_input in inputs
            )
            print(f"\r{line}", end="", flush=True)
            time.sleep(args.interval)
    finally:
        print()
        for _name, _pin, gpio_input in inputs:
            gpio_input.close()


def pulse(args):
    configure_gpio_backend(CONFIG.gpio.gpio_chip)
    outputs = _build_outputs(CONFIG)
    try:
        if args.button not in outputs:
            raise ValueError(f"unknown button {args.button!r}")
        output = outputs[args.button]
        print(f"Driving {args.button} ON for {args.duration}s")
        output.write(GpioState.ON)
        time.sleep(args.duration)
        output.write(GpioState.OFF)
        print(f"Driving {args.button} OFF")
    finally:
        for output in outputs.values():
            output.write(GpioState.OFF)
            output.close()


def main():
    parser = argparse.ArgumentParser(description="Probe configured Raspberry Pi GPIO pins.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    read_parser = subparsers.add_parser("read", help="Continuously print configured input levels.")
    read_parser.add_argument("--interval", type=float, default=0.1)

    pulse_parser = subparsers.add_parser("pulse", help="Pulse a configured pushbutton drive pin.")
    pulse_parser.add_argument("button", choices=("pb1", "pb2", "pb3"))
    pulse_parser.add_argument("--duration", type=float, default=1.0)

    args = parser.parse_args()
    if args.command == "read":
        read_loop(args)
    elif args.command == "pulse":
        pulse(args)


if __name__ == "__main__":
    main()
