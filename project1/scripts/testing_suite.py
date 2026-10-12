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


def main():
    open(CONFIG.log_file, "w", encoding="utf-8").close()
    digital_twin = build_digital_twin(CONFIG)
    physical_twin = build_physical_twin(
        CONFIG,
        drive_pushbuttons=(CONFIG.mode == TestMode.AUTOMATED),
    )
    tui = Tui()
    logger = TwinEventLogger(CONFIG.log_file)

    digital_twin.main()
    threading.Thread(
        target=logger_loop,
        args=(logger, digital_twin, physical_twin, CONFIG),
        daemon=True,
    ).start()
    threading.Thread(
        target=tui_loop,
        args=(tui, digital_twin, physical_twin, CONFIG),
        daemon=True,
    ).start()

    if CONFIG.mode == TestMode.AUTOMATED:
        threading.Thread(
            target=automated_command_loop,
            args=(digital_twin, physical_twin, CONFIG),
            daemon=True,
        ).start()
    else:
        threading.Thread(
            target=manual_input_loop,
            args=(digital_twin, physical_twin, CONFIG),
            daemon=True,
        ).start()

    while True:
        time.sleep(100)


if __name__ == "__main__":
    main()
