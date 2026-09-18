"""Plot measured memory hierarchy latency (cycles/hop vs buffer size) for the Apple M3 Pro.

Every value is parsed from results/2026-09-16/l1_residency.txt. Nothing is
hardcoded, so rerunning the sweep and regenerating the plot stays in sync.
"""

import re

import pandas as pd
import matplotlib.pyplot as plt

TXT_PATH = "results/2026-09-16/l1_residency.txt"
OUT_PATH = "results/2026-09-16/latency_hierarchy.png"

SERIES_COLOR = "#2a78d6"
REF_COLOR = "#898781"
L1_SHADE = "#dce9f8"
L2_SHADE = "#eceade"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID_COLOR = "#e1e0d9"
AXIS_COLOR = "#c3c2b7"
SURFACE = "#fcfcfb"

UNIT_BYTES = {"KB": 1024, "MB": 1024 * 1024}

ROW_RE = re.compile(
    r"^\s*(\d+)\s+(KB|MB)\s+([\d.]+)\s+([\d,]+)\s+~?([\d.]+)", re.MULTILINE
)
L1_CAPACITY_RE = re.compile(r"capacity ratio\s+(\d+)\s*(KB|MB)\s*/\s*buffer")
L1_LATENCY_RE = re.compile(r"measured\s*=\s*f\*([\d.]+)")
L2_LATENCY_RE = re.compile(r"L2 latency is\s+([\d.]+)\s+cycles")
L2_SOLVE_RE = re.compile(r"^\s*(\d+)\s+(KB|MB)\s+f\s*=\s*[\d.]+\s+L\s*=", re.MULTILINE)


def parse(path):
    """Pull the sweep table and the reference latencies out of the raw log."""
    text = open(path).read()

    rows = []
    for size, unit, cycles, misses, f_measured in ROW_RE.findall(text):
        rows.append(
            {
                "bytes": int(size) * UNIT_BYTES[unit],
                "label": f"{size} {unit}",
                "cycles_per_hop": float(cycles),
                "l1_misses": int(misses.replace(",", "")),
                "f_measured": float(f_measured),
            }
        )
    df = pd.DataFrame(rows).sort_values("bytes").reset_index(drop=True)

    size, unit = L1_CAPACITY_RE.search(text).groups()
    l1_capacity = int(size) * UNIT_BYTES[unit]

    # Largest buffer the file still solves the two-population model at; past
    # this point the walk is out of L2.
    l2_sizes = [int(s) * UNIT_BYTES[u] for s, u in L2_SOLVE_RE.findall(text)]

    return {
        "df": df,
        "l1_capacity": l1_capacity,
        "l2_extent": max(l2_sizes),
        "l1_latency": float(L1_LATENCY_RE.search(text).group(1)),
        "l2_latency": float(L2_LATENCY_RE.search(text).group(1)),
    }


def main():
    data = parse(TXT_PATH)
    df = data["df"]

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    ax.set_xscale("log", base=2)

    left = df["bytes"].min() / 1.6
    right = df["bytes"].max() * 1.6

    ax.axvspan(left, data["l1_capacity"], color=L1_SHADE, zorder=0)
    ax.axvspan(data["l1_capacity"], data["l2_extent"], color=L2_SHADE, zorder=0)

    ax.plot(
        df["bytes"],
        df["cycles_per_hop"],
        color=SERIES_COLOR,
        linewidth=2,
        marker="o",
        markersize=6,
        markerfacecolor=SERIES_COLOR,
        markeredgecolor=SURFACE,
        markeredgewidth=0.75,
        zorder=3,
    )

    for _, row in df.iterrows():
        ax.annotate(
            f"{row['cycles_per_hop']:.1f}",
            (row["bytes"], row["cycles_per_hop"]),
            textcoords="offset points",
            xytext=(0, 9),
            ha="center",
            color=TEXT_PRIMARY,
            fontsize=8.5,
            zorder=4,
        )

    top = df["cycles_per_hop"].max() * 1.12
    ax.set_ylim(0, top)
    ax.set_xlim(left, right)

    ax.text(
        (left * data["l1_capacity"]) ** 0.5,
        top * 0.94,
        f"L1-resident\n(≤ {data['l1_capacity'] // 1024} KB)",
        color=TEXT_SECONDARY,
        fontsize=9,
        ha="center",
        va="top",
    )
    ax.text(
        (data["l1_capacity"] * data["l2_extent"]) ** 0.5,
        top * 0.94,
        f"L2\n(~{data['l2_latency']:.1f} cycles)",
        color=TEXT_SECONDARY,
        fontsize=9,
        ha="center",
        va="top",
    )
    ax.text(
        (data["l2_extent"] * right) ** 0.5,
        top * 0.94,
        "DRAM",
        color=TEXT_SECONDARY,
        fontsize=9,
        ha="center",
        va="top",
    )

    for latency, label in [
        (data["l1_latency"], f"L1 hit, {data['l1_latency']:.3f} cycles"),
        (data["l2_latency"], f"L2, {data['l2_latency']:.1f} cycles"),
    ]:
        ax.axhline(latency, color=REF_COLOR, linestyle="--", linewidth=1.25, zorder=2)
        ax.text(
            right,
            latency,
            label,
            color=TEXT_SECONDARY,
            fontsize=9,
            va="bottom",
            ha="right",
        )

    ax.set_xlabel("buffer size (bytes, log scale)", color=TEXT_SECONDARY, fontsize=10)
    ax.set_ylabel("cycles / hop (dependent load chain)", color=TEXT_SECONDARY, fontsize=10)

    ax.grid(True, axis="y", color=GRID_COLOR, linewidth=0.75, zorder=1)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(AXIS_COLOR)

    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)
    ax.set_xticks(df["bytes"])
    ax.set_xticklabels(df["label"])
    ax.minorticks_off()

    fig.tight_layout()
    fig.savefig(OUT_PATH, facecolor=SURFACE)
    print(f"saved {OUT_PATH}")


if __name__ == "__main__":
    main()
