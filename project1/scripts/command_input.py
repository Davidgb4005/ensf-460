import time

from testing_config import DEFAULT_COMMANDS


def load_commands(path):
    if path is None:
        return list(DEFAULT_COMMANDS)

    commands = []
    with open(path, "r", encoding="utf-8") as command_file:
        for line_number, raw_line in enumerate(command_file, start=1):
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue

            parts = line.split()
            command = parts[0].lower()
            args = tuple(parts[1:])
            if command not in {"press", "wait"}:
                raise ValueError(f"{path}:{line_number}: unknown command {command!r}")
            commands.append((command, args))

    return commands


def run_commands(commands, digital_twin, physical_twin):
    buttons = {
        "pb1": (digital_twin.pb_1, physical_twin.pb_1),
        "pb2": (digital_twin.pb_2, physical_twin.pb_2),
        "pb3": (digital_twin.pb_3, physical_twin.pb_3),
    }

    for command, args in commands:
        if command == "wait":
            if len(args) != 1:
                raise ValueError("wait expects: wait <seconds>")
            time.sleep(float(args[0]))
            continue

        if command == "press":
            if len(args) != 2:
                raise ValueError("press expects: press <pb1|pb2|pb3|pb1+pb2> <seconds>")
            names = args[0].lower().replace(",", "+").split("+")
            duration = float(args[1])
            for name in names:
                if name not in buttons:
                    raise ValueError(f"unknown button {name!r}")
                digital_button, physical_button = buttons[name]
                digital_button.press_button(duration)
                physical_button.press_button(duration)
