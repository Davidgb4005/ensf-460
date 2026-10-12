import argparse
import copy
import threading
import time

from command_input import load_commands, run_commands
from digital_twin import build_digital_twin
from event_logger import TwinEventLogger
from physical_twin import build_physical_twin, sync_button_from_physical
from testing_config import CONFIG, TestMode
from tui_output import Tui


def manual_input_loop(digital_twin, physical_twin, config):
    while True:
        sync_button_from_physical(digital_twin.pb_1, physical_twin.pb_1)
        sync_button_from_physical(digital_twin.pb_2, physical_twin.pb_2)
        sync_button_from_physical(digital_twin.pb_3, physical_twin.pb_3)
        time.sleep(config.timing.manual_sample_s)


def tui_loop(tui, digital_twin, physical_twin, config):
    while True:
        tui.print(digital_twin, physical_twin)
        time.sleep(config.timing.tui_refresh_s)


def automated_command_loop(digital_twin, physical_twin, config):
    commands = load_commands(config.command_file)
    run_commands(commands, digital_twin, physical_twin)


def logger_loop(logger, digital_twin, physical_twin, config):
    while True:
        logger.poll(digital_twin, physical_twin)
        time.sleep(config.timing.log_poll_s)


def build_runtime_config():
    parser = argparse.ArgumentParser(description="Run the twin TUI harness.")
    parser.add_argument(
        "command_file",
        nargs="?",
        help="Optional command file to run in automated mode while showing the TUI.",
    )
    args = parser.parse_args()

    config = copy.deepcopy(CONFIG)
    if args.command_file is not None:
        config.mode = TestMode.AUTOMATED
        config.command_file = args.command_file
    return config


def main():
    config = build_runtime_config()
    open(config.log_file, "w", encoding="utf-8").close()
    digital_twin = build_digital_twin(config)
    physical_twin = build_physical_twin(
        config,
        drive_pushbuttons=(config.mode == TestMode.AUTOMATED),
    )
    tui = Tui()
    logger = TwinEventLogger(config.log_file)

    digital_twin.main()
    threading.Thread(
        target=logger_loop,
        args=(logger, digital_twin, physical_twin, config),
        daemon=True,
    ).start()
    threading.Thread(
        target=tui_loop,
        args=(tui, digital_twin, physical_twin, config),
        daemon=True,
    ).start()

    if config.mode == TestMode.AUTOMATED:
        threading.Thread(
            target=automated_command_loop,
            args=(digital_twin, physical_twin, config),
            daemon=True,
        ).start()
    else:
        threading.Thread(
            target=manual_input_loop,
            args=(digital_twin, physical_twin, config),
            daemon=True,
        ).start()

    while True:
        time.sleep(100)


if __name__ == "__main__":
    main()
