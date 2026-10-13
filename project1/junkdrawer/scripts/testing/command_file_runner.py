import time

from _junkdrawer.scripts.hardware.test_harness_config import DEFAULT_COMMANDS


VALID_COMMANDS = {
    "expect",
    "expect_range",
    "note",
    "press",
    "wait",
    "wait_until",
}

STARTUP_RESET_COMMANDS = (
    ("press", ("pb3", "5")),
    ("wait", ("5.5",)),
    ("press", ("pb1+pb2", "5")),
    ("wait", ("5.5",)),
    ("expect", ("state", "IDLE")),
    ("expect", ("preset", "0")),
    ("expect", ("elapsed", "0")),
)


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
            if command not in VALID_COMMANDS:
                raise ValueError(f"{path}:{line_number}: unknown command {command!r}")
            commands.append((command, args))

    return commands


def _enum_name(value):
    return value.name.removeprefix("TIMER_")


def _normal_name(value):
    return value.upper().removeprefix("TIMER_")


def _read_field(digital_twin, field):
    field = field.lower()
    if field == "state":
        return _enum_name(digital_twin.state)
    if field == "preset":
        return digital_twin.preset_timer
    if field == "elapsed":
        return digital_twin.elapsed_timer
    if field == "pb1":
        return _enum_name(digital_twin.pb_1.state)
    if field == "pb2":
        return _enum_name(digital_twin.pb_2.state)
    if field == "pb3":
        return _enum_name(digital_twin.pb_3.state)
    if field == "led1_state":
        return _enum_name(digital_twin.led_1.state)
    if field == "led1_output":
        return _enum_name(digital_twin.led_1.output)
    if field == "led2_state":
        return _enum_name(digital_twin.led_2.state)
    if field == "led2_output":
        return _enum_name(digital_twin.led_2.output)
    raise ValueError(f"unknown field {field!r}")


def _matches(actual, expected):
    if isinstance(actual, str):
        return actual == _normal_name(expected)
    return actual == int(expected)


def _expect(digital_twin, field, expected):
    actual = _read_field(digital_twin, field)
    if not _matches(actual, expected):
        raise AssertionError(f"expected {field}={expected}, got {actual}")


def _expect_range(digital_twin, field, minimum, maximum):
    actual = _read_field(digital_twin, field)
    if not isinstance(actual, int):
        raise AssertionError(f"expect_range only works on numeric fields, got {field}={actual}")
    minimum = int(minimum)
    maximum = int(maximum)
    if actual < minimum or actual > maximum:
        raise AssertionError(f"expected {field} between {minimum} and {maximum}, got {actual}")


def _wait_until(digital_twin, field, expected, timeout_s):
    deadline = time.monotonic() + float(timeout_s)
    while time.monotonic() <= deadline:
        actual = _read_field(digital_twin, field)
        if _matches(actual, expected):
            return
        time.sleep(0.01)
    actual = _read_field(digital_twin, field)
    raise AssertionError(f"timed out waiting for {field}={expected}, got {actual}")


def run_commands(commands, digital_twin, physical_twin=None, startup_reset=True):
    buttons = {
        "pb1": (digital_twin.pb_1, physical_twin.pb_1 if physical_twin is not None else None),
        "pb2": (digital_twin.pb_2, physical_twin.pb_2 if physical_twin is not None else None),
        "pb3": (digital_twin.pb_3, physical_twin.pb_3 if physical_twin is not None else None),
    }

    if startup_reset:
        commands = (*STARTUP_RESET_COMMANDS, *commands)

    for command, args in commands:
        if command == "note":
            continue

        if command == "wait":
            if len(args) != 1:
                raise ValueError("wait expects: wait <seconds>")
            time.sleep(float(args[0]))
            continue

        if command == "expect":
            if len(args) != 2:
                raise ValueError("expect expects: expect <field> <value>")
            _expect(digital_twin, args[0], args[1])
            continue

        if command == "expect_range":
            if len(args) != 3:
                raise ValueError("expect_range expects: expect_range <field> <minimum> <maximum>")
            _expect_range(digital_twin, args[0], args[1], args[2])
            continue

        if command == "wait_until":
            if len(args) != 3:
                raise ValueError("wait_until expects: wait_until <field> <value> <timeout_s>")
            _wait_until(digital_twin, args[0], args[1], args[2])
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
                if physical_button is not None:
                    physical_button.press_button(duration)
