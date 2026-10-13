import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

from hardware.config import CONFIG


def load_power_csv(path):
    data = pd.read_csv(path)
    required_columns = {"elapsed_s", "current_ua"}
    missing = required_columns - set(data.columns)
    if missing:
        raise ValueError(f"{path} missing columns: {', '.join(sorted(missing))}")
    return data.dropna(subset=["elapsed_s", "current_ua"])


def summarize(data, voltage_v):
    elapsed_s = data["elapsed_s"].to_numpy()
    current_a = data["current_ua"].to_numpy() / 1_000_000.0

    if len(data) == 0:
        raise ValueError("CSV has no power samples")

    duration_s = float(elapsed_s[-1] - elapsed_s[0]) if len(data) > 1 else 0.0
    avg_current_a = float(current_a.mean())
    max_current_a = float(current_a.max())
    avg_power_w = avg_current_a * voltage_v
    max_power_w = max_current_a * voltage_v

    if len(data) > 1:
        charge_c = float((current_a[:-1] * (elapsed_s[1:] - elapsed_s[:-1])).sum())
    else:
        charge_c = 0.0

    return {
        "duration_s": duration_s,
        "avg_current_ua": avg_current_a * 1_000_000.0,
        "max_current_ua": max_current_a * 1_000_000.0,
        "avg_power_mw": avg_power_w * 1_000.0,
        "max_power_mw": max_power_w * 1_000.0,
        "charge_mc": charge_c * 1_000.0,
        "charge_mah": charge_c / 3600.0 * 1000.0,
    }


def plot(data, summary, voltage_v, title, output):
    elapsed_s = data["elapsed_s"]
    current_ua = data["current_ua"]

    fig, ax = plt.subplots(figsize=(12, 6))
    ax.plot(elapsed_s, current_ua, linewidth=0.8)
    ax.set_title(title)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Current (uA)")
    ax.grid(True, alpha=0.3)

    summary_text = "\n".join(
        (
            f"Voltage: {voltage_v:.3f} V",
            f"Duration: {summary['duration_s']:.3f} s",
            f"Avg current: {summary['avg_current_ua']:.3f} uA",
            f"Max current: {summary['max_current_ua']:.3f} uA",
            f"Avg power: {summary['avg_power_mw']:.6f} mW",
            f"Max power: {summary['max_power_mw']:.6f} mW",
            f"Charge: {summary['charge_mc']:.6f} mC",
            f"Charge: {summary['charge_mah']:.9f} mAh",
        )
    )
    ax.text(
        0.99,
        0.98,
        summary_text,
        transform=ax.transAxes,
        va="top",
        ha="right",
        bbox={"boxstyle": "round", "facecolor": "white", "alpha": 0.85},
    )

    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150)
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description="Plot a PPK2 CSV capture.")
    parser.add_argument("csv_file", help="CSV file from run_test_suite.py --power-profile.")
    parser.add_argument(
        "--voltage",
        type=float,
        default=CONFIG.power.source_voltage_mv / 1000.0,
        help="DUT voltage in volts. Defaults to testing_config.py power voltage.",
    )
    parser.add_argument("--output", help="Save plot image instead of opening a window.")
    args = parser.parse_args()

    path = Path(args.csv_file)
    data = load_power_csv(path)
    summary = summarize(data, args.voltage)
    title = f"Power Capture: {path.name}"

    print(f"Duration: {summary['duration_s']:.3f} s")
    print(f"Avg current: {summary['avg_current_ua']:.3f} uA")
    print(f"Max current: {summary['max_current_ua']:.3f} uA")
    print(f"Avg power: {summary['avg_power_mw']:.6f} mW")
    print(f"Max power: {summary['max_power_mw']:.6f} mW")
    print(f"Charge: {summary['charge_mc']:.6f} mC")
    print(f"Charge: {summary['charge_mah']:.9f} mAh")

    plot(data, summary, args.voltage, title, args.output)


if __name__ == "__main__":
    main()
