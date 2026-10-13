# Standalone TUI

`terminal.py` is independent of the twins, logger, and test runner. The rest of
the rig shares one `TuiSharedState` instance with it.

```python
from testing.TUI.terminal import (
    DashboardData,
    OutputData,
    TerminalTUI,
    TestData,
    TuiCommand,
    TuiSharedState,
    TwinData,
)

shared = TuiSharedState()
tui = TerminalTUI(shared)
tui.start()

shared.publish(
    DashboardData(
        digital=TwinData(state="RUN", elapsed_seconds=1, preset_seconds=10),
        hardware=TwinData(state="RUN", elapsed_seconds=1, preset_seconds=10),
        test=TestData(
            run_id=1,
            current_name="short countdown",
            current_path="tests/short.txt",
            next_name="alarm test",
            next_path="tests/alarm.txt",
            duration_seconds=10,
            status="RUNNING",
        ),
        outputs=OutputData(
            hardware_uart_path="logs/hardware-uart.log",
            digital_uart_path="logs/digital-uart.log",
            power_profiler_status="CONNECTED",
            hardware_uart_port="/dev/serial0",
        ),
    )
)
```

The twin/controller passes timestamped UART lines directly to the display
transport. This does not depend on a logger being present:

```python
shared.append_digital_uart("[00:00:01.120] State: RUN")
shared.append_hardware_uart("[00:00:01.123] State: RUN")
```

When logging is also enabled, the twin/controller sends the same event to the
logger independently. The logger never forwards messages to the TUI, so either
component can be enabled without the other.

The controller consumes commands without any controller callback executing on
the curses thread:

```python
command = shared.get_command(block=True)
if command is TuiCommand.RESTART_TEST:
    restart_current_test()
```

Increment `run_id` whenever a test starts, including when restarting the same
test. Call `tui.stop()` and `tui.join()` during external shutdown. A user-issued
`Ctrl+Q` enqueues `TuiCommand.QUIT` and exits the TUI thread itself.
