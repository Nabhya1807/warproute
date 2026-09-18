"""Plot measured L1 residency against the capacity prediction f = L1 / buffer.

Every value is parsed from results/2026-09-16/l1_residency.txt, including the
nominal L1 capacity used in the prediction. The gap between the two series is
the ~79% effective-residency correction.
"""

import re

import pandas as pd
import matplotlib.pyplot as plt

TXT_PATH = "results/2026-09-16/l1_residency.txt"
OUT_PATH = "results/2026-09-16/l1_residency.png"

MEASURED_COLOR = "#2a78d6"
PREDICTED_COLOR = "#898781"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID_COLOR = "#e1e0d9"
AXIS_COLOR = "#c3c2b7"
SURFACE = "#fcfcfb"

UNIT_BYTES = {"KB": 1024, "MB": 1024 * 1024}

# The file prints a ratio column only where both fractions are precise enough
# for it to mean anything; that presence is used as the labelling mask below.
ROW_RE = re.compile(
    r"^\s*(\d+)\s+(KB|MB)\s+([\d.]+)\s+([\d,]+)\s+~?([\d.]+)"
    r"(?:\s+~?([\d.]+))?(?:\s+([\d.]+))?\s*$",
    re.MULTILINE,
)
L1_CAPACITY_RE = re.compile(r"capacity ratio\s+(\d+)\s*(KB|MB)\s*/\s*buffer")


def parse(path):
    """Pull the sweep table and the nominal L1 capacity out of the raw log."""
    text = open(path).read()

    rows = []
    for size, unit, cycles, misses, f_measured, _f_cap, ratio in ROW_RE.findall(text):
        rows.append(
            {
                "bytes": int(size) * UNIT_BYTES[unit],
                "label": f"{size} {unit}",
                "cycles_per_hop": float(cycles),
                "l1_misses": int(misses.replace(",", "")),
                "f_measured": float(f_measured),
                "file_ratio": float(ratio) if ratio else None,
            }
        )
    df = pd.DataFrame(rows).sort_values("bytes").reset_index(drop=True)

    size, unit = L1_CAPACITY_RE.search(text).groups()
    l1_capacity = int(size) * UNIT_BYTES[unit]

    # The model's prediction, capped at 1.0: a buffer smaller than the cache
    # is fully resident.
    df["f_capacity"] = (l1_capacity / df["bytes"]).clip(upper=1.0)
    df["ratio"] = df["f_measured"] / df["f_capacity"]

    return df, l1_capacity


def main():
    df, l1_capacity = parse(TXT_PATH)

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")

    ax.plot(
        df["bytes"],
        df["f_capacity"],
        color=PREDICTED_COLOR,
        linewidth=2,
        linestyle="--",
        marker="s",
        markersize=5,
        markerfacecolor=SURFACE,
        markeredgecolor=PREDICTED_COLOR,
        markeredgewidth=1.5,
        label=f"f predicted = {l1_capacity // 1024} KB / buffer",
        zorder=2,
    )
    ax.plot(
        df["bytes"],
        df["f_measured"],
        color=MEASURED_COLOR,
        linewidth=2,
        marker="o",
        markersize=6,
        markerfacecolor=MEASURED_COLOR,
        markeredgecolor=SURFACE,
        markeredgewidth=0.75,
        label="f measured = 1 - (L1 load misses / hops)",
        zorder=3,
    )

    labelled = df[df["file_ratio"].notna()]
    for _, row in labelled.iterrows():
        ax.annotate(
            f"{row['ratio']:.3f}",
            (row["bytes"], row["f_measured"]),
            textcoords="offset points",
            xytext=(0, -16),
            ha="center",
            color=MEASURED_COLOR,
            fontsize=8.5,
            zorder=4,
        )

    ax.annotate(
        "measured / predicted, converging on "
        f"{labelled['ratio'].iloc[-1]:.2f} from above",
        xy=(df["bytes"].iloc[2], df["f_measured"].iloc[2]),
        xytext=(0.06, 0.28),
        textcoords="axes fraction",
        color=TEXT_PRIMARY,
        fontsize=9,
        arrowprops=dict(arrowstyle="-", color=AXIS_COLOR, linewidth=1),
    )

    ax.set_xlabel("buffer size (bytes, log scale)", color=TEXT_SECONDARY, fontsize=10)
    ax.set_ylabel("fraction of accesses resident in L1 (log scale)", color=TEXT_SECONDARY, fontsize=10)

    ax.grid(True, color=GRID_COLOR, linewidth=0.75, zorder=0)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(AXIS_COLOR)

    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)
    ax.set_xticks(df["bytes"])
    ax.set_xticklabels(df["label"])
    ax.minorticks_off()

    legend = ax.legend(frameon=False, fontsize=9, loc="upper right")
    for text in legend.get_texts():
        text.set_color(TEXT_SECONDARY)

    fig.tight_layout()
    fig.savefig(OUT_PATH, facecolor=SURFACE)
    print(f"saved {OUT_PATH}")


if __name__ == "__main__":
    main()
