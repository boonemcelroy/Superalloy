#!/usr/bin/env python3
"""
Superalloy — plotResponse.py
Renders one analyser-style frequency-response graph per metal from the CSV
written by measureSuperalloy, plus one overlay of all metals.

Usage: python plotResponse.py <response.csv> <out-dir> [sampleRate]
Needs: matplotlib, numpy
"""
import csv
import collections
import math
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.ticker import FixedLocator, FuncFormatter, MultipleLocator
import numpy as np

# ------------------------------------------------------------------ metals
# Menu order and resistivity, mirroring Source/MetalTable.h.
METALS = [
    ("Superconductor (NbTi, 0 Ω reference)", 0.0),
    ("Silver — Ohno Continuous Cast 7N", 15.87e-9),
    ("Copper — Grade 1 6N OFC", 16.78e-9),
    ("Gold — pure", 22.14e-9),
    ("Rhodium", 43.0e-9),
    ("Iridium", 49.0e-9),
    ("Platinum-Iridium 10", 250.0e-9),
    ("Monel 400", 0.53e-6),
    ("Haynes 188", 1.01e-6),
    ("MP35N", 1.03e-6),
    ("Waspaloy", 1.22e-6),
    ("Inconel 718", 1.24e-6),
    ("Hastelloy C-276", 1.26e-6),
    ("Inconel 625", 1.29e-6),
    ("René 41", 1.29e-6),
]
COPPER = 2

# ------------------------------------------------------------------ style
SURFACE = "#1a1a19"
PANEL = "#141413"
GRID = "#2e2d2a"
GRID_MAJOR = "#3a3935"
TEXT = "#f0efec"
TEXT_2 = "#c3c2b7"
TEXT_3 = "#8f8e86"
BAND = "#222220"            # outside 20 Hz – 20 kHz
SERIES_QUIET = "#3987e5"    # −18 dBFS (dark slot 1, validated)
SERIES_HOT = "#d95926"      # 0 dBFS (dark slot 2, validated)
REFERENCE = "#7d7c75"       # copper reference (neutral, dashed)

for fam in ("Inter", "DejaVu Sans"):
    if any(fam == f.name for f in font_manager.fontManager.ttflist):
        plt.rcParams["font.family"] = fam
        break
plt.rcParams.update({
    "figure.facecolor": SURFACE,
    "axes.facecolor": PANEL,
    "axes.edgecolor": GRID_MAJOR,
    "axes.labelcolor": TEXT_2,
    "xtick.color": TEXT_3,
    "ytick.color": TEXT_3,
    "text.color": TEXT,
    "font.size": 11,
})

MAG_LIM = (-0.25, 0.05)
PHASE_LIM = (-1.0, 17.0)


def hz_label(v, _pos=None):
    if v >= 1000:
        k = v / 1000.0
        return f"{k:g}k"
    return f"{v:g}"


def load(path):
    data = collections.defaultdict(list)
    with open(path, newline="") as fh:
        for r in csv.DictReader(fh):
            data[(int(r["metal"]), float(r["level_dbfs"]))].append(
                (float(r["freq_hz"]), float(r["mag_db"]), float(r["phase_deg"])))
    out = {}
    for k, rows in data.items():
        rows.sort()
        a = np.array(rows)
        out[k] = (a[:, 0], a[:, 1], a[:, 2])
    return out


def sgn(v, nd):
    r = round(v, nd)
    if r == 0:
        return f"{0:.{nd}f}"
    return f"{r:+.{nd}f}"


def value_at(series, f):
    fr, mag, ph = series
    i = int(np.argmin(np.abs(fr - f)))
    return mag[i], ph[i]


def setup_x(ax, fmax):
    ax.set_xscale("log")
    ax.set_xlim(10, fmax)
    majors = [v for v in (10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
                          10000, 20000, 50000, 100000) if v <= fmax]
    ax.xaxis.set_major_locator(FixedLocator(majors))
    ax.xaxis.set_major_formatter(FuncFormatter(hz_label))
    minors = [m * d for d in (10, 100, 1000, 10000, 100000) for m in range(2, 10)
              if m * d <= fmax and m * d not in majors]
    ax.xaxis.set_minor_locator(FixedLocator(minors))
    ax.xaxis.set_minor_formatter(FuncFormatter(lambda *_: ""))
    ax.grid(which="major", color=GRID_MAJOR, linewidth=0.8)
    ax.grid(which="minor", color=GRID, linewidth=0.5)
    ax.axvspan(10, 20, color=BAND, zorder=0, linewidth=0)
    ax.axvspan(20000, fmax, color=BAND, zorder=0, linewidth=0)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.tick_params(length=0, pad=6)


def end_label(ax, x, y, text, color, dy=0.0):
    ax.annotate(text, xy=(x, y), xytext=(6, dy), textcoords="offset points",
                color=TEXT_2, fontsize=10, va="center", ha="left",
                annotation_clip=False)
    ax.plot([x], [y], marker="o", markersize=4.5, color=color,
            markeredgecolor=PANEL, markeredgewidth=1.5, zorder=5, clip_on=False)


def slug(name):
    s = name.split("(")[0].split("—")[0]
    s = s.replace("é", "e").replace("Ω", "")
    return re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_")


def plot_metal(m, data, fs, outdir):
    name, rho = METALS[m]
    quiet = data[(m, -18.0)]
    hot = data[(m, 0.0)]
    ref = data[(COPPER, -18.0)]
    fmax = float(quiet[0][-1])

    fig = plt.figure(figsize=(14, 8.6), dpi=150)
    gs = fig.add_gridspec(2, 1, height_ratios=[2.15, 1], hspace=0.12,
                          left=0.07, right=0.965, top=0.78, bottom=0.115)
    axm = fig.add_subplot(gs[0])
    axp = fig.add_subplot(gs[1], sharex=axm)

    # header
    fig.text(0.07, 0.945, "SUPERALLOY", color=TEXT_3, fontsize=11,
             fontweight="bold")
    fig.text(0.07, 0.895, name, color=TEXT, fontsize=24, fontweight="bold")
    rho_txt = "0 Ω·m (reference)" if rho == 0 else (
        f"{rho * 1e9:.2f} nΩ·m" if rho < 1e-6 else f"{rho * 1e6:.2f} µΩ·m")
    fig.text(0.07, 0.855,
             f"Resistivity {rho_txt}  ·  {fs / 1000:g} kHz  ·  "
             f"measured from the plugin processor",
             color=TEXT_2, fontsize=11.5)

    # readouts (top right)
    m20, p20 = value_at(quiet, 20)
    m1k, p1k = value_at(quiet, 1000)
    m20k, p20k = value_at(quiet, 20000)
    band = (quiet[0] >= 20) & (quiet[0] <= 20000)
    rdev = quiet[1][band] - ref[1][band]
    worst = float(rdev[np.argmax(np.abs(rdev))])
    cells = [("20 Hz", f"{sgn(m20, 4)} dB", f"{sgn(p20, 2)}°"),
             ("1 kHz", f"{sgn(m1k, 4)} dB", f"{sgn(p1k, 2)}°"),
             ("20 kHz", f"{sgn(m20k, 4)} dB", f"{sgn(p20k, 2)}°")]
    x0 = 0.60
    for i, (f, mg, ph) in enumerate(cells):
        x = x0 + i * 0.105
        fig.text(x, 0.945, f, color=TEXT_3, fontsize=10)
        fig.text(x, 0.912, mg, color=TEXT, fontsize=13, fontweight="bold")
        fig.text(x, 0.882, ph, color=TEXT_2, fontsize=11)
    fig.text(x0, 0.852, "Readouts at −18 dBFS", color=TEXT_3, fontsize=9.5)
    if m != COPPER:
        fig.text(0.07, 0.822, f"Worst shape vs copper, 20 Hz – 20 kHz, −18 dBFS:  {sgn(worst, 4)} dB   "
                 f"(cap ±0.1 dB)", color=TEXT_3, fontsize=10.5)

    # magnitude
    setup_x(axm, fmax)
    axm.set_ylim(*MAG_LIM)
    axm.yaxis.set_major_locator(MultipleLocator(0.05))
    axm.yaxis.set_minor_locator(MultipleLocator(0.01))
    axm.grid(which="minor", axis="y", color=GRID, linewidth=0.4)
    axm.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+.2f}" if abs(v) > 1e-9 else "0"))
    axm.set_ylabel("Magnitude (dB)")
    axm.axhline(0, color=TEXT_3, linewidth=0.9, zorder=1)
    if m != COPPER:
        axm.plot(ref[0], ref[1], color=REFERENCE, linewidth=1.3, linestyle=(0, (4, 3)), zorder=2)
    axm.plot(hot[0], hot[1], color=SERIES_HOT, linewidth=2.0, zorder=3)
    axm.plot(quiet[0], quiet[1], color=SERIES_QUIET, linewidth=2.0, zorder=4)
    plt.setp(axm.get_xticklabels(), visible=False)
    axm.text(14.5, MAG_LIM[1] - 0.012, "20 Hz", color=TEXT_3, fontsize=9, ha="center", va="top")
    axm.text(20000 * 1.04, MAG_LIM[1] - 0.012, "20 kHz", color=TEXT_3, fontsize=9, ha="left", va="top")

    handles = [plt.Line2D([], [], color=SERIES_QUIET, lw=2, label="−18 dBFS"),
               plt.Line2D([], [], color=SERIES_HOT, lw=2, label="0 dBFS")]
    if m != COPPER:
        handles.append(plt.Line2D([], [], color=REFERENCE, lw=1.3, ls=(0, (4, 3)),
                                  label="Copper, −18 dBFS (reference)"))
    leg = axm.legend(handles=handles, loc="lower right", frameon=False, fontsize=10,
                     labelcolor=TEXT_2, ncol=len(handles), handlelength=2.6,
                     bbox_to_anchor=(1.0, 0.0))

    # phase
    setup_x(axp, fmax)
    axp.set_ylim(*PHASE_LIM)
    axp.yaxis.set_major_locator(MultipleLocator(4))
    axp.yaxis.set_minor_locator(MultipleLocator(1))
    axp.grid(which="minor", axis="y", color=GRID, linewidth=0.4)
    axp.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}°" if v else "0°"))
    axp.set_ylabel("Phase")
    axp.set_xlabel("Frequency (Hz)")
    axp.axhline(0, color=TEXT_3, linewidth=0.9, zorder=1)
    if m != COPPER:
        axp.plot(ref[0], ref[2], color=REFERENCE, linewidth=1.3, linestyle=(0, (4, 3)), zorder=2)
    axp.plot(hot[0], hot[2], color=SERIES_HOT, linewidth=2.0, zorder=3)
    axp.plot(quiet[0], quiet[2], color=SERIES_QUIET, linewidth=2.0, zorder=4)

    fig.text(0.07, 0.022,
             "Stepped sine, raised-cosine fade-in, least-squares fit of the fundamental.  "
             "Shaded: outside 20 Hz – 20 kHz.  0 dB = input level.  "
             "Same scales on every graph so metals compare directly.",
             color=TEXT_3, fontsize=9.5)

    path = os.path.join(outdir, f"Superalloy_FR_{m:02d}_{slug(name)}.png")
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)
    return path


def plot_overlay(data, fs, outdir):
    """All metals at −18 dBFS; one sequential hue, light→dark by resistivity."""
    ramp = ["#cde2fb", "#b7d3f6", "#9ec5f4", "#86b6ef", "#6da7ec", "#5598e7",
            "#3987e5", "#2a78d6", "#256abf", "#1c5cab", "#184f95", "#104281"]
    fig = plt.figure(figsize=(14, 8.6), dpi=150)
    gs = fig.add_gridspec(2, 1, height_ratios=[2.15, 1], hspace=0.12,
                          left=0.07, right=0.80, top=0.82, bottom=0.115)
    axm = fig.add_subplot(gs[0])
    axp = fig.add_subplot(gs[1], sharex=axm)
    fmax = float(data[(0, -18.0)][0][-1])
    fig.text(0.07, 0.945, "SUPERALLOY", color=TEXT_3, fontsize=11, fontweight="bold")
    fig.text(0.07, 0.895, "All metals", color=TEXT, fontsize=24, fontweight="bold")
    fig.text(0.07, 0.86, f"Frequency response at −18 dBFS  ·  {fs / 1000:g} kHz  ·  "
             "lighter = lower resistivity", color=TEXT_2, fontsize=11.5)
    setup_x(axm, fmax)
    setup_x(axp, fmax)
    axm.set_ylim(*MAG_LIM)
    axm.yaxis.set_major_locator(MultipleLocator(0.05))
    axm.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+.2f}" if abs(v) > 1e-9 else "0"))
    axm.set_ylabel("Magnitude (dB)")
    axp.set_ylim(*PHASE_LIM)
    axp.yaxis.set_major_locator(MultipleLocator(4))
    axp.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}°" if v else "0°"))
    axp.set_ylabel("Phase")
    axp.set_xlabel("Frequency (Hz)")
    plt.setp(axm.get_xticklabels(), visible=False)
    for ax in (axm, axp):
        ax.axhline(0, color=TEXT_3, linewidth=0.9, zorder=1)

    # identical-resistivity metals share one trace and one label
    groups = collections.OrderedDict()
    for m, (name, rho) in enumerate(METALS):
        groups.setdefault(rho, []).append(m)
    keys = list(groups.keys())
    idx = np.linspace(0, len(ramp) - 1, len(keys)).round().astype(int)
    labels = []
    for gi, rho in enumerate(keys):
        ms = groups[rho]
        s = data[(ms[0], -18.0)]
        col = ramp[idx[gi]]
        axm.plot(s[0], s[1], color=col, linewidth=1.6, zorder=3)
        axp.plot(s[0], s[2], color=col, linewidth=1.6, zorder=3)
        name = " / ".join(METALS[m][0].split(" (")[0].split(" —")[0] for m in ms)
        labels.append((s[1][0], name, col))

    # stacked label column at the right, ordered by 10 Hz value, with leaders
    labels.sort(key=lambda t: -t[0])
    lax = fig.add_axes([0.815, 0.115, 0.18, 0.705])
    lax.axis("off")
    for i, (_, name, col) in enumerate(labels):
        y = 1 - (i + 0.5) / len(labels)
        lax.plot([0.0, 0.08], [y, y], color=col, linewidth=2.4, transform=lax.transAxes)
        lax.text(0.12, y, name, color=TEXT_2, fontsize=10, va="center", transform=lax.transAxes)
    path = os.path.join(outdir, "Superalloy_FR_all_metals.png")
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)
    return path


def main():
    src = sys.argv[1]
    outdir = sys.argv[2]
    fs = float(sys.argv[3]) if len(sys.argv) > 3 else 352800.0
    os.makedirs(outdir, exist_ok=True)
    data = load(src)
    for m in range(len(METALS)):
        print(plot_metal(m, data, fs, outdir))
    print(plot_overlay(data, fs, outdir))


if __name__ == "__main__":
    main()
