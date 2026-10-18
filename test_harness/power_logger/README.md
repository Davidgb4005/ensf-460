# ensf-460

## Nordic PPK2 TCP recorder

`ppk_tcp_server.py` operates a Nordic Power Profiler Kit II in **Ampere Meter
mode** and listens for the TCP commands `start`, `complete`, `power_on`, and
`power_off`.
`start <test_filename>` creates a new CSV capture; `complete` stops measurement
and closes that CSV. Stopping a capture does **not** turn off DUT power. Only an explicit
`power_off` command does that. The server preserves the explicitly requested
power state during shutdown and avoids the upstream API destructor that resets
the PPK2.

Install and run:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python ppk_tcp_server.py --port /dev/ttyACM0 --voltage-mv 3300 --csv
```

The defaults are TCP `0.0.0.0:5000`, an external supply value of 3300 mV, and
the `captures/` directory. If exactly one PPK2 is attached, `--port` can be
omitted. In Ampere Meter mode, `--voltage-mv` describes the external voltage
for measurement calibration; the PPK2 does not generate or regulate it.

Wire the externally powered DUT in series:

```text
External supply +  -> PPK2 VIN
PPK2 VOUT          -> DUT VCC
External supply -  -> PPK2 GND -> DUT GND
```

Do not connect the external supply directly to DUT VCC as well, because that
would bypass the PPK2 measurement path. Keep the external supply between 0.8 V
and 5.0 V and do not exceed the PPK2's 1 A continuous Ampere Meter limit. The
`power_on` and `power_off` commands connect and disconnect the VIN-to-VOUT path;
they do not change the external supply voltage.

Output flags are independent:

```bash
# CSV reduced from the hardware's 100 kS/s to 1,000 rows/second
python ppk_tcp_server.py --csv --sample 1000

# Animated Matplotlib view only (the requested spelling and corrected alias work)
python ppk_tcp_server.py --live-veiw --sample-avg 100000
python ppk_tcp_server.py --live-view

# Display and save simultaneously at 1,000 CSV rows/second
python ppk_tcp_server.py --live-veiw --csv --sample 1000

# Save current plus the raw GPIO mask and digital channels D0-D7
python ppk_tcp_server.py --csv --sample 1000 --gpio-sample
```

Without either output flag, measurement data is consumed but neither displayed
nor saved. The live plot displays a decimated five-second window to keep the GUI
responsive. CSV retention is controlled separately with `--sample`.

The PPK2 always acquires at 100 kS/s. `--sample RATE` controls how many rows per
second are retained in the CSV, from 1 through 100000. It does not change the
hardware acquisition or live plot. The default is the full 100000 rows/second;
`--sample 1000` reduces CSV growth by approximately 100 times while retaining
the original sample numbers and timestamps.

GPIO columns are disabled by default to reduce file size. Add `--gpio-sample`
to include `digital_bits` and the eight `digital_0` through `digital_7` columns.

`--sample-avg SAMPLES` sets the raw-sample sliding window used for the live
average and maximum current annotation. The default is 100000 samples, or one
second at the fixed PPK2 acquisition rate. The graph also displays total charge
in Ah accumulated since the most recent `start`. These statistics are displayed
as text and are not plotted as additional traces.

Every command is terminated by a newline. The server buffers fragmented TCP
data until that newline arrives, so it does not depend on TCP packet boundaries:

```bash
printf 'start heavy_load_test\n' | nc 127.0.0.1 5000
printf 'complete\n' | nc 127.0.0.1 5000
printf 'power_off\n' | nc 127.0.0.1 5000
printf 'power_on\n' | nc 127.0.0.1 5000
```

With `--csv`, `start heavy_load_test` writes
`captures/heavy_load_test.csv`. Existing files are never overwritten. Path
components are rejected, and unsupported filename characters are replaced with
underscores.

Commands are fire-and-forget: the server sends no response bytes because the
Rust client does not read them. Results and errors are written to the server
log. Each CSV contains the 100 ksample/s sample index and derived UTC timestamp,
current in microamps, the raw digital bit mask, and all eight digital input
channels.

Run the hardware-free tests with:

```bash
python -m unittest -v test_ppk_tcp_server.py
```

## Compare two captures

`compare_power_csv.py` removes the first two seconds from both captures and
truncates the longer capture to the remaining row count of the shorter one. It
then creates three panels—each trace individually followed by an overlay—with
both traces drawn at 0.7 alpha. Each trace's average current and integrated
charge are displayed on the figure:

```bash
python compare_power_csv.py captures/test_a.csv captures/test_b.csv
```

To save the graph without opening a window:

```bash
python compare_power_csv.py captures/test_a.csv captures/test_b.csv \
  --output comparison.png --no-show
```

The comparison uses two streaming passes so even large CSV inputs do not need
to be loaded fully into memory. At most 200000 points per trace are drawn by
default; all matched rows are still used for the averages and charge totals.
