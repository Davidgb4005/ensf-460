# Test Rig Logger

The logger has no TUI or twin dependency. The top-level controller supplies its
three paths and calls it when an event occurs.

```python
from testing.logger import LoggerPaths, TestRigLogger

logger = TestRigLogger(
    LoggerPaths(
        digital_uart="logs/digital-uart.log",
        hardware_uart="logs/hardware-uart.log",
        state_events="logs/state-events.csv",
    )
)

digital = {"state": "RUN", "elapsed": 1, "pb1": "OFF"}
hardware = {"state": "RUN", "elapsed": 1, "pb1": "OFF"}

logger.log_state_change("STATE: IDLE->RUN", digital, hardware)
logger.log_uart("digital", "[00:00:01.120] State: RUN", digital, hardware)
```

`log_uart` writes the unchanged message to the selected pure UART file and a
second record containing both complete state mappings to the CSV event log.
`log_state_change` writes only to the event log.
