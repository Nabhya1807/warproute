"""Plot the Day 7 blocked SGEMM tile sweep, padded against unpadded.

Both CSVs carry n = 512 and n = 1024. This plots n = 1024, the power-of-two
stride where the conflict-miss effect shows up; set N below to 512 for the
other size.
"""

import pandas as pd
import matplotlib.pyplot as plt

UNPADDED_PATH = "results/2026-09-10/sgemm_tile_sweep.csv"
PADDED_PATH = "results/2026-09-10/sgemm_tile_sweep_padded.csv"
OUT_PATH = "results/2026-09-10/sgemm_tile_sweep.png"

N = 1024

PADDED_COLOR = "#2a78d6"
UNPADDED_COLOR = "#898781"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID_COLOR = "#e1e0d9"
AXIS_COLOR = "#c3c2b7"
SURFACE = "#fcfcfb"


def load(path):
    df = pd.read_csv(path)
    return df[df["n"] == N].sort_values("tile").reset_index(drop=True)


def main():
    unpadded = load(UNPADDED_PATH)
    padded = load(PADDED_PATH)

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    ax.set_xscale("log", base=2)

    for df, color, marker, label in [
        (unpadded, UNPADDED_COLOR, "s", "unpadded"),
        (padded, PADDED_COLOR, "o", "padded"),
    ]:
        ax.plot(
            df["tile"],
            df["gflops"],
            color=color,
            linewidth=2,
            marker=marker,
            markersize=6,
            markerfacecolor=color,
            markeredgecolor=SURFACE,
            markeredgewidth=0.75,
            label=label,
            zorder=3,
        )

    # Mark the tile size where padding buys the most.
    merged = unpadded.merge(padded, on="tile", suffixes=("_unpadded", "_padded"))
    merged["gain"] = merged["gflops_padded"] / merged["gflops_unpadded"]
    best = merged.loc[merged["gain"].idxmax()]

    ax.annotate(
        "",
        xy=(best["tile"], best["gflops_padded"]),
        xytext=(best["tile"], best["gflops_unpadded"]),
        arrowprops=dict(arrowstyle="<->", color=TEXT_SECONDARY, linewidth=1.1),
        zorder=4,
    )
    ax.annotate(
        f"{best['gain']:.2f}x at T = {int(best['tile'])}",
        (best["tile"], (best["gflops_padded"] + best["gflops_unpadded"]) / 2),
        textcoords="offset points",
        xytext=(10, 0),
        va="center",
        color=TEXT_PRIMARY,
        fontsize=9.5,
        zorder=4,
    )

    ax.set_xlabel(f"tile size T (elements, log scale), n = {N}", color=TEXT_SECONDARY, fontsize=10)
    ax.set_ylabel("GFLOP/s", color=TEXT_SECONDARY, fontsize=10)
    ax.set_ylim(0, max(unpadded["gflops"].max(), padded["gflops"].max()) * 1.15)

    ax.grid(True, axis="y", color=GRID_COLOR, linewidth=0.75, zorder=0)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(AXIS_COLOR)

    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)
    ax.set_xticks(unpadded["tile"])
    ax.set_xticklabels([int(t) for t in unpadded["tile"]])
    ax.minorticks_off()

    legend = ax.legend(frameon=False, fontsize=9, loc="upper right")
    for text in legend.get_texts():
        text.set_color(TEXT_SECONDARY)

    fig.tight_layout()
    fig.savefig(OUT_PATH, facecolor=SURFACE)
    print(f"saved {OUT_PATH}")
    print(f"  largest padding gain: {best['gain']:.3f}x at T = {int(best['tile'])}")


if __name__ == "__main__":
    main()
