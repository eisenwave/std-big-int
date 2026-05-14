# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
"""Plot multiplication-algorithm crossover points from crossover_tuning_data.csv.

Reads the CSV produced by multiplication_stress_bench.test.cpp (columns:
algorithm,limbs,trials,ns_per_mul) and generates PNGs that make it easy to
see where each higher-order algorithm starts paying off:

  crossover_main.png        log-log ns/multiplication vs operand size,
                            one line per algorithm. Vertical dashed lines mark
                            empirically detected crossover points.

  crossover_speedup.png     each algorithm's speedup over schoolbook
                            (>1 means faster than schoolbook at that size).

  crossover_normalized.png  ns / limbs^omega for each algorithm where omega
                            is the theoretical asymptotic exponent. Curves
                            that flatten at large sizes confirm the algorithm
                            hits its expected complexity.

A text summary (crossover_summary.txt) is also written with the fitted
log-log slope vs theoretical complexity and the detected crossover limb
counts between adjacent algorithm tiers.

Usage:
    python3 plot_crossover.py [csv_path] [--out-dir DIR]

Dependencies: matplotlib only (no pandas/numpy).
"""

import argparse
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

try:
    import matplotlib

    matplotlib.use("Agg")  # headless: no display required
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib is required. Install with: pip install matplotlib", file=sys.stderr)
    sys.exit(1)


# Algorithm tier order from slowest-asymptotic to fastest-asymptotic.
ALGO_ORDER = [
    "schoolbook",
    "karatsuba",
    "toom-cook-3",
    "toom-cook-4",
    "toom-cook-6.5",
]

# Theoretical complexity exponent omega such that ns ~ limbs^omega.
THEORETICAL_OMEGA = {
    "schoolbook": 2.0,
    "karatsuba": math.log2(3),                            # ~1.585
    "toom-cook-3": math.log(5) / math.log(3),             # ~1.465
    "toom-cook-4": math.log(7) / math.log(4),             # ~1.404
    "toom-cook-6.5": math.log(12) / math.log(6.5),        # ~1.328
}

# Distinct colors per algorithm.
COLORS = {
    "schoolbook": "#1f77b4",
    "karatsuba": "#ff7f0e",
    "toom-cook-3": "#2ca02c",
    "toom-cook-4": "#d62728",
    "toom-cook-6.5": "#9467bd",
}


def load_csv(path):
    """Return {algorithm: [(limbs, ns), ...]} sorted by limbs."""
    rows = defaultdict(list)
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows[row["algorithm"]].append((int(row["limbs"]), float(row["ns_per_mul"])))
    for algo in rows:
        rows[algo].sort()
    return rows


def fit_log_log_slope(points, min_limb=None):
    """Least-squares slope of log(ns) vs log(limbs).

    Pass min_limb to drop the constant-overhead head so the fit reflects
    asymptotic behaviour rather than fixed per-call setup time.
    """
    if min_limb is not None:
        points = [p for p in points if p[0] >= min_limb]
    if len(points) < 2:
        return None
    xs = [math.log(limbs) for (limbs, _) in points]
    ys = [math.log(ns) for (_, ns) in points]
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((xs[i] - mx) * (ys[i] - my) for i in range(n))
    den = sum((xs[i] - mx) ** 2 for i in range(n))
    return num / den if den else None


def find_crossover(data_a, data_b):
    """Estimate the limb count at which algorithm B overtakes algorithm A.

    Uses linear interpolation in log-log space between the two consecutive
    overlapping measurements that bracket the crossover. Returns None if no
    crossover is observed in the overlap of measured sizes.
    """
    a_map = dict(data_a)
    b_map = dict(data_b)
    common = sorted(set(a_map.keys()) & set(b_map.keys()))
    if len(common) < 2:
        return None
    prev_diff = None
    prev_limb = None
    for limbs in common:
        diff = math.log(b_map[limbs]) - math.log(a_map[limbs])  # <0 once B is faster
        if prev_diff is not None and prev_diff > 0 >= diff:
            # Linear-interp in log-log between (prev_limb, prev_diff) and (limbs, diff).
            t = prev_diff / (prev_diff - diff)
            log_x = math.log(prev_limb) + t * (math.log(limbs) - math.log(prev_limb))
            return int(round(math.exp(log_x)))
        if prev_diff is not None and prev_diff <= 0:
            return prev_limb  # B was already faster at the first overlapping point
        prev_diff = diff
        prev_limb = limbs
    return None


def detect_crossovers(data):
    """Return list of (left, right, crossover_limbs|None) for adjacent tiers."""
    out = []
    for i in range(len(ALGO_ORDER) - 1):
        left, right = ALGO_ORDER[i], ALGO_ORDER[i + 1]
        if left in data and right in data:
            out.append((left, right, find_crossover(data[left], data[right])))
    return out


def plot_main(data, crossovers, output_path):
    """ns/mul vs limbs (log-log) with crossover markers."""
    fig, ax = plt.subplots(figsize=(11, 6.5))
    for algo in ALGO_ORDER:
        if algo not in data:
            continue
        xs = [limbs for (limbs, _) in data[algo]]
        ys = [ns for (_, ns) in data[algo]]
        ax.plot(xs, ys, marker="o", markersize=4, linewidth=1.5,
                color=COLORS[algo], label=algo)
    # Stagger crossover annotations across three y-levels inside the plot so
    # closely-spaced crossovers don't overlap each other and none of them clip
    # the title (which sits above the axes).
    annotation_ys = [0.95, 0.85, 0.75]
    for idx, (left, right, x) in enumerate(crossovers):
        if x is None:
            continue
        ax.axvline(x, color=COLORS[right], linestyle="--", alpha=0.4)
        ax.annotate(
            f"-> {right} @ {x}",
            xy=(x, annotation_ys[idx % len(annotation_ys)]),
            xycoords=("data", "axes fraction"),
            xytext=(4, 0), textcoords="offset points",
            ha="left", va="center", fontsize=8,
            color=COLORS[right],
            bbox=dict(boxstyle="round,pad=0.2", fc="white", ec="none", alpha=0.75),
        )
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Operand size (64-bit limbs)")
    ax.set_ylabel("ns per multiplication")
    ax.set_title("Multiplication algorithm timings (log-log)")
    ax.legend(loc="upper left")
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, dpi=120)
    plt.close(fig)


def plot_speedup(data, crossovers, output_path):
    """One subplot per adjacent-tier transition; each shows the speedup
    of the higher-order algorithm over the lower-order one across the
    pair's actual overlapping limb range.

    Speedup = left_ns / right_ns. A horizontal break-even line at y=1 makes
    the crossover trivially visible (the curve crosses y=1 there).
    """
    crossover_map = {(left, right): x for (left, right, x) in crossovers}

    pairs = []
    for i in range(len(ALGO_ORDER) - 1):
        left, right = ALGO_ORDER[i], ALGO_ORDER[i + 1]
        if left not in data or right not in data:
            continue
        left_map = dict(data[left])
        right_map = dict(data[right])
        common = sorted(set(left_map.keys()) & set(right_map.keys()))
        if len(common) >= 2:
            pairs.append((left, right, common, left_map, right_map))

    if not pairs:
        return False

    n = len(pairs)
    cols = 2 if n > 1 else 1
    rows = (n + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(11, 4 * rows), squeeze=False)

    for idx, (left, right, common, left_map, right_map) in enumerate(pairs):
        ax = axes[idx // cols][idx % cols]
        ys = [left_map[limbs] / right_map[limbs] for limbs in common]
        ax.plot(common, ys, marker="o", markersize=4, linewidth=1.5,
                color=COLORS[right])
        ax.axhline(1.0, color="gray", linestyle="--", alpha=0.5)
        cx = crossover_map.get((left, right))
        if cx is not None:
            ax.axvline(cx, color=COLORS[right], linestyle="--", alpha=0.4)
            ax.annotate(
                f"crossover @ {cx}",
                xy=(cx, 1.0),
                xytext=(4, 4), textcoords="offset points",
                fontsize=8, color=COLORS[right],
                bbox=dict(boxstyle="round,pad=0.2", fc="white", ec="none", alpha=0.75),
            )
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("Operand size (64-bit limbs)")
        ax.set_ylabel(f"{left} time / {right} time")
        ax.set_title(f"{left}  ->  {right}")
        ax.grid(True, which="both", alpha=0.3)

    # Hide any unused subplots in the grid (e.g., if pairs is odd).
    for idx in range(n, rows * cols):
        axes[idx // cols][idx % cols].axis("off")

    fig.suptitle(
        "Speedup ratio per adjacent-tier transition (>1 means right column is faster)",
        fontsize=12,
    )
    fig.tight_layout()
    fig.savefig(output_path, dpi=120)
    plt.close(fig)
    return True


def plot_normalized(data, output_path):
    """ns / limbs^omega for each algorithm. Asymptote = matches theory."""
    fig, ax = plt.subplots(figsize=(11, 6.5))
    for algo in ALGO_ORDER:
        if algo not in data:
            continue
        omega = THEORETICAL_OMEGA[algo]
        xs = [limbs for (limbs, _) in data[algo]]
        ys = [ns / (limbs ** omega) for (limbs, ns) in data[algo]]
        ax.plot(xs, ys, marker="o", markersize=4, linewidth=1.5,
                color=COLORS[algo], label=f"{algo} (omega={omega:.3f})")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Operand size (64-bit limbs)")
    ax.set_ylabel("ns / limbs^omega")
    ax.set_title("Time normalized by theoretical complexity (flat tail = matches theory)")
    ax.legend(loc="best")
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, dpi=120)
    plt.close(fig)


def build_summary(data, crossovers):
    """Build a multi-line text summary of fits and crossover points."""
    lines = []
    lines.append("Multiplication algorithm crossover summary")
    lines.append("=" * 50)
    lines.append("")
    lines.append("Empirical complexity (log-log slope of upper-half data)")
    lines.append("-" * 50)
    lines.append(f"{'algorithm':<15} {'empirical':>10} {'theoretical':>13}")
    for algo in ALGO_ORDER:
        if algo not in data:
            continue
        pts = data[algo]
        # Drop the lower half so constant overhead doesn't bias the slope.
        tail_cutoff = pts[len(pts) // 2][0] if len(pts) >= 4 else None
        slope = fit_log_log_slope(pts, min_limb=tail_cutoff)
        theory = THEORETICAL_OMEGA[algo]
        slope_str = f"{slope:.3f}" if slope is not None else "n/a"
        lines.append(f"{algo:<15} {slope_str:>10} {theory:>13.3f}")
    lines.append("")
    lines.append("Crossover points (first limb count where right is faster than left)")
    lines.append("-" * 50)
    lines.append(f"{'left':<15} -> {'right':<15} {'crossover':>12}")
    for (left, right, x) in crossovers:
        x_str = f"{x} limbs" if x is not None else "not observed"
        lines.append(f"{left:<15} -> {right:<15} {x_str:>12}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "csv", nargs="?",
        default=str(Path(__file__).parent / "crossover_tuning_data.csv"),
        help="path to crossover_tuning_data.csv",
    )
    parser.add_argument(
        "--out-dir", default=None,
        help="directory to write PNGs and summary into (default: alongside CSV)",
    )
    args = parser.parse_args()

    csv_path = Path(args.csv).resolve()
    if not csv_path.exists():
        print(f"error: CSV not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out_dir).resolve() if args.out_dir else csv_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    data = load_csv(csv_path)
    if not data:
        print(f"error: no rows in {csv_path}", file=sys.stderr)
        sys.exit(1)

    n_rows = sum(len(v) for v in data.values())
    print(f"loaded {n_rows} rows for {len(data)} algorithms from {csv_path}")

    crossovers = detect_crossovers(data)

    plot_main(data, crossovers, out_dir / "crossover_main.png")
    print(f"wrote {out_dir / 'crossover_main.png'}")
    if plot_speedup(data, crossovers, out_dir / "crossover_speedup.png"):
        print(f"wrote {out_dir / 'crossover_speedup.png'}")
    plot_normalized(data, out_dir / "crossover_normalized.png")
    print(f"wrote {out_dir / 'crossover_normalized.png'}")

    summary = build_summary(data, crossovers)
    summary_path = out_dir / "crossover_summary.txt"
    summary_path.write_text(summary)
    print(f"wrote {summary_path}")
    print()
    print(summary, end="")


if __name__ == "__main__":
    main()
