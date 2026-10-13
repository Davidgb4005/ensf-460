# Power profiler

This package is the small Nordic PPK2 adapter used by the test harness. The
harness calls `start(test_name)` immediately before a test and `stop()` when
the test ends. Each test gets one CSV containing only:

```text
elapsed_seconds,current_ua
```

The elapsed time is derived from the PPK2's fixed 100 kS/s sample rate, so no
timestamps need to be passed in from the harness.

## Setup

Install the API dependency:

```bash
python -m pip install -r testing/power_profiler/requirements.txt
```

Create the profiler and pass it to `TestHarness`:

```python
from pathlib import Path
from testing.power_profiler import PowerProfilerConfig, Ppk2PowerProfiler

profiler = Ppk2PowerProfiler(
    PowerProfilerConfig(
        output_directory=Path("test-output/power"),
        port="/dev/ttyACM0",       # omit to auto-detect one PPK2
        mode="source",             # or "ampere" for an externally powered DUT
        voltage_mv=3300,
    )
)
```

Source mode configures the PPK2 output voltage but does not switch DUT power.
Power sequencing remains the responsibility of the hardware setup. In ampere
mode, `voltage_mv` is the external DUT supply voltage used by the API.

Only one program can own the PPK2 serial port. Close Nordic's Power Profiler
application before running the harness.
