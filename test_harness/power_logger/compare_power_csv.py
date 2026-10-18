#!/usr/bin/env python3
"""Plot two PPK2 CSV captures after trimming and length matching them."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
from typing import Iterator, Optional, Sequence, Tuple


TRIM_SECONDS = 2.0
UA_SECONDS_PER_AH = 1_000_000 * 3600


def capture_rows(path: Path) -> Iterator[Tuple[float, float]]:
    """Yield (elapsed seconds, current uA) after the first two seconds."""
    with path.open(newline="") as csv_file:
        reader = csv.DictReader(csv_file)
        required = {"elapsed_us", "current_uA"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"{path} must contain elapsed_us and current_uA columns")
        for line_number, row in enumerate(reader, start=2):
            try:
                elapsed = float(row["elapsed_us"]) / 1_000_000
                current = float(row["current_uA"])
            except (TypeError, ValueError) as error:
                raise ValueError(f"invalid measurement in {path} at line {line_number}") from error
            if elapsed >= TRIM_SECONDS:
                yield elapsed, current


def retained_row_count(path: Path) -> int:
    return sum(1 for _ in capture_rows(path))


def comparison_data(
    first_path: Path, second_path: Path, max_plot_points: int = 200_000
) -> Tuple[dict, dict, int]:
    """Read equally many post-trim rows and retain a bounded plotting subset."""
    matched_count = min(retained_row_count(first_path), retained_row_count(second_path))
    if matched_count < 1:
        raise ValueError("both captures need at least one sample after the first two seconds")

    plot_stride = max(1, math.ceil(matched_count / max_plot_points))
    results = [
        {"times": [], "currents": [], "sum": 0.0, "charge_ah": 0.0},
        {"times": [], "currents": [], "sum": 0.0, "charge_ah": 0.0},
    ]
    first_starts: list[Optional[float]] = [None, None]
    previous: list[Optional[Tuple[float, float]]] = [None, None]

    paired_rows = zip(capture_rows(first_path), capture_rows(second_path))
    for index, rows in enumerate(paired_rows):
        if index >= matched_count:
            break
        for capture_index, (elapsed, current) in enumerate(rows):
            result = results[capture_index]
            if first_starts[capture_index] is None:
                first_starts[capture_index] = elapsed
            result["sum"] += current

            old = previous[capture_index]
            if old is not None:
                old_elapsed, old_current = old
                delta_seconds = elapsed - old_elapsed
                if delta_seconds < 0:
                    raise ValueError("CSV elapsed_us values must be monotonically increasing")
                result["charge_ah"] += (
                    (old_current + current) * 0.5 * delta_seconds / UA_SECONDS_PER_AH
                )
            previous[capture_index] = (elapsed, current)

            if index % plot_stride == 0 or index == matched_count - 1:
                result["times"].append(elapsed - first_starts[capture_index])
                result["currents"].append(current)

    for result in results:
        result["average_uA"] = result.pop("sum") / matched_count
    return results[0], results[1], matched_count


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first_csv", type=Path)
    parser.add_argument("second_csv", type=Path)
    parser.add_argument("--output", type=Path, help="also save the graph to this image")
    parser.add_argument(
        "--max-plot-points",
        type=int,
        default=200_000,
        help="maximum displayed points per trace (default: 200000)",
    )
    parser.add_argument("--no-show", action="store_true", help="do not open a GUI window")
    args = parser.parse_args(argv)
    if args.max_plot_points < 1:
        parser.error("--max-plot-points must be at least 1")
    if args.no_show and args.output is None:
        parser.error("--no-show requires --output")
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    first, second, matched_count = comparison_data(
        args.first_csv, args.second_csv, args.max_plot_points
    )

    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise RuntimeError("matplotlib is required; run: pip install -r requirements.txt") from error

    figure, axes = plt.subplots(3, 1, figsize=(12, 12), sharex=True)
    labels = [args.first_csv.stem, args.second_csv.stem]
    colors = ["tab:blue", "tab:orange"]
    results = [first, second]

    for axis, result, label, color in zip(axes[:2], results, labels, colors):
        axis.plot(
            result["times"],
            result["currents"],
            linewidth=1,
            alpha=0.7,
            color=color,
            label=label,
        )
        axis.set_title(label)
        axis.set_ylabel("Current (uA)")
        axis.grid(True)
        axis.legend()
        axis.text(
            0.02,
            0.96,
            f"Average: {result['average_uA']:.3f} uA\n"
            f"Charge: {result['charge_ah']:.9f} Ah",
            transform=axis.transAxes,
            va="top",
            bbox={"boxstyle": "round", "facecolor": "white", "alpha": 0.8},
        )

    overlay = axes[2]
    for result, label, color in zip(results, labels, colors):
        overlay.plot(
            result["times"],
            result["currents"],
            linewidth=1,
            alpha=0.7,
            color=color,
            label=label,
        )
    overlay.set_title("Overlay")
    overlay.set_xlabel("Time after trim (s)")
    overlay.set_ylabel("Current (uA)")
    overlay.grid(True)
    overlay.legend()
    summary = "\n".join(
        f"{label}: avg {result['average_uA']:.3f} uA, "
        f"charge {result['charge_ah']:.9f} Ah"
        for label, result in zip(labels, results)
    )
    overlay.text(
        0.02,
        0.98,
        summary,
        transform=overlay.transAxes,
        va="top",
        bbox={"boxstyle": "round", "facecolor": "white", "alpha": 0.8},
    )
    figure.suptitle(
        f"PPK2 comparison: first 2 s removed, {matched_count:,} matched rows"
    )
    figure.tight_layout(rect=(0, 0, 1, 0.97))

    if args.output is not None:
        figure.savefig(args.output, dpi=150)
    if not args.no_show:
        plt.show()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
