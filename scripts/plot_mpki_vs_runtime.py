"""Plot L1d MPKI against runtime for naive and padded blocked SGEMM.

Both panels are computed from the Day 9 counter CSVs:
    MPKI    = l1d_cache_miss_ld_nonspec / instructions * 1000
    runtime = cycles
Both runs of each kernel are plotted, so the run-to-run spread stays visible
rather than being averaged away.
"""

import pandas as pd
import matplotlib.pyplot as plt

RUN_PATHS = {
    1: "results/2026-09-16/day9_sgemm_counters_run1.csv",
    2: "results/2026-09-16/day9_sgemm_counters_run2.csv",
}
OUT_PATH = "results/2026-09-16/day9_mpki_vs_runtime.png"

KERNEL_ORDER = ["naive", "padded_blocked"]
KERNEL_COLOR = {"naive": "#898781", "padded_blocked": "#2a78d6"}
RUN_ALPHA = {1: 1.0, 2: 0.55}

TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID_COLOR = "#e1e0d9"
AXIS_COLOR = "#c3c2b7"
SURFACE = "#fcfcfb"

BAR_WIDTH = 0.34


def load():
    frames = []
    for run, path in RUN_PATHS.items():
        df = pd.read_csv(path)
        df["run"] = run
        frames.append(df)
    df = pd.concat(frames, ignore_index=True)

    df["mpki"] = df["l1d_cache_miss_ld_nonspec"] / df["instructions"] * 1000
    df["gcycles"] = df["cycles"] / 1e9
    return df


def ratio_range(df, column, invert=False):
    """Per-run naive/padded ratio, returned as (low, high) across the runs."""
    wide = df.pivot(index="run", columns="kernel", values=column)
    ratio = wide["naive"] / wide["padded_blocked"]
    if invert:
        ratio = 1 / ratio
    return ratio.min(), ratio.max()


def draw_panel(ax, df, column, ylabel, log_scale, value_fmt):
    for offset, run in zip((-BAR_WIDTH / 2, BAR_WIDTH / 2), sorted(RUN_PATHS)):
        sub = df[df["run"] == run].set_index("kernel").loc[KERNEL_ORDER]
        positions = [i + offset for i in range(len(KERNEL_ORDER))]
        ax.bar(
            positions,
            sub[column],
            width=BAR_WIDTH,
            color=[KERNEL_COLOR[k] for k in KERNEL_ORDER],
            alpha=RUN_ALPHA[run],
            edgecolor=SURFACE,
            linewidth=0.75,
            label=f"run {run}",
            zorder=3,
        )
        for pos, value in zip(positions, sub[column]):
            ax.annotate(
                value_fmt.format(value),
                (pos, value),
                textcoords="offset points",
                xytext=(0, 4),
                ha="center",
                color=TEXT_PRIMARY,
                fontsize=8.5,
                zorder=4,
            )

    if log_scale:
        ax.set_yscale("log")
        ax.set_ylim(top=df[column].max() * 4)
    else:
        ax.set_ylim(0, df[column].max() * 1.28)

    ax.set_ylabel(ylabel, color=TEXT_SECONDARY, fontsize=10)
    ax.set_xticks(range(len(KERNEL_ORDER)))
    ax.set_xticklabels(["naive", "padded blocked\n(T=32, ld=1040)"])
    ax.grid(True, axis="y", color=GRID_COLOR, linewidth=0.75, zorder=0)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(AXIS_COLOR)
    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)


def annotate_drop(ax, df, column, low, high, label, log_scale):
    """Draw a bracket between the two kernels carrying the measured ratio."""
    top = df[column].max()
    y = top * 2.1 if log_scale else top * 1.14
    tick = y / 1.25 if log_scale else y - top * 0.045

    ax.plot([0, 0, 1, 1], [tick, y, y, tick], color=AXIS_COLOR, linewidth=1, zorder=2)
    ax.annotate(
        label.format(low=low, high=high),
        (0.5, y),
        textcoords="offset points",
        xytext=(0, 5),
        ha="center",
        color=TEXT_PRIMARY,
        fontsize=10,
        zorder=4,
    )


def main():
    df = load()

    mpki_low, mpki_high = ratio_range(df, "mpki")
    speed_low, speed_high = ratio_range(df, "cycles")

    fig, axes = plt.subplots(1, 2, figsize=(10, 5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    for ax in axes:
        ax.set_facecolor(SURFACE)

    draw_panel(
        axes[0],
        df,
        "mpki",
        "L1d misses per 1000 instructions (log scale)",
        log_scale=True,
        value_fmt="{:.2f}",
    )
    annotate_drop(
        axes[0], df, "mpki", mpki_low, mpki_high,
        "{low:.0f}x fewer misses", log_scale=True,
    )

    draw_panel(
        axes[1],
        df,
        "gcycles",
        "runtime (billion cycles)",
        log_scale=False,
        value_fmt="{:.2f}",
    )
    annotate_drop(
        axes[1], df, "gcycles", speed_low, speed_high,
        "{low:.2f}–{high:.2f}x faster", log_scale=False,
    )

    handles, labels = axes[0].get_legend_handles_labels()
    legend = fig.legend(
        handles,
        labels,
        frameon=False,
        fontsize=9,
        loc="lower center",
        ncol=2,
        bbox_to_anchor=(0.5, -0.01),
    )
    for text in legend.get_texts():
        text.set_color(TEXT_SECONDARY)

    fig.tight_layout(rect=(0, 0.05, 1, 1))
    fig.savefig(OUT_PATH, facecolor=SURFACE)
    print(f"saved {OUT_PATH}")
    print(f"  MPKI ratio  {mpki_low:.2f}x - {mpki_high:.2f}x")
    print(f"  speedup     {speed_low:.3f}x - {speed_high:.3f}x")


if __name__ == "__main__":
    main()
