"""Plot L1d associativity sweep results (ns/hop vs K) for the Apple M3 Pro."""

import pandas as pd
import matplotlib.pyplot as plt

CSV_PATH = "results/assoc_m3pro.csv"
OUT_PATH = "results/assoc_m3pro.png"

SERIES_COLOR = "#2a78d6"
REF_COLOR = "#898781"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID_COLOR = "#e1e0d9"
AXIS_COLOR = "#c3c2b7"
SURFACE = "#fcfcfb"

L1_HIT_LATENCY = 1.54
ASSOC_K = 8


def main():
    df = pd.read_csv(CSV_PATH)
    df = df[df["K"] != 1]  # exclude degenerate K=1 case

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    ax.plot(
        df["K"],
        df["ns_per_hop"],
        color=SERIES_COLOR,
        linewidth=2,
        marker="o",
        markersize=6,
        markerfacecolor=SERIES_COLOR,
        markeredgecolor=SURFACE,
        markeredgewidth=0.75,
        zorder=3,
    )

    ax.axvline(ASSOC_K, color=REF_COLOR, linestyle="--", linewidth=1.25, zorder=2)
    ax.text(
        ASSOC_K + 0.15,
        df["ns_per_hop"].max() * 0.95,
        "8 ways",
        color=TEXT_SECONDARY,
        fontsize=9,
        va="top",
    )

    ax.axhline(L1_HIT_LATENCY, color=REF_COLOR, linestyle="--", linewidth=1.25, zorder=2)
    ax.text(
        df["K"].max() - 0.2,
        L1_HIT_LATENCY,
        "L1 hit latency",
        color=TEXT_SECONDARY,
        fontsize=9,
        va="bottom",
        ha="right",
    )

    ax.set_title("L1d associativity on Apple M3 Pro", color=TEXT_PRIMARY, fontsize=13, pad=12)
    ax.set_xlabel("K (ways probed)", color=TEXT_SECONDARY, fontsize=10)
    ax.set_ylabel("ns / hop", color=TEXT_SECONDARY, fontsize=10)

    ax.grid(True, color=GRID_COLOR, linewidth=0.75, zorder=0)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(AXIS_COLOR)

    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)
    ax.set_xticks(df["K"])

    fig.tight_layout()
    fig.savefig(OUT_PATH, facecolor=SURFACE)
    print(f"saved {OUT_PATH}")


if __name__ == "__main__":
    main()
