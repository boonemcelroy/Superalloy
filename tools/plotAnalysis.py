#!/usr/bin/env python3
"""
Superalloy — plotAnalysis.py
All-metals analysis graphs from the CSVs written by measureSuperalloy
(response.csv) and analyzeSuperalloy (thd_freq, thd_level, imd, step, null).

Every metal keeps one colour AND one line style on every graph. Fifteen
colours cannot all be told apart by colour alone (especially with colour-
vision deficiency), so the line style and the legend column are the
second identifier, not decoration.

Usage: python plotAnalysis.py <response.csv> <analysis-dir> <out-dir> [sampleRate]
Needs: matplotlib, numpy, pandas
"""
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.colors import LinearSegmentedColormap, TwoSlopeNorm
from matplotlib.lines import Line2D
from matplotlib.ticker import FixedLocator, FuncFormatter, MultipleLocator, NullFormatter
import numpy as np
import pandas as pd

# ---------------------------------------------------------------- metals
# (menu name, short label, resistivity Ω·m, colour, line style)
SOLID, DASH, DOT, DASHDOT = "-", (0, (6, 3)), (0, (1.2, 2.2)), (0, (7, 2.5, 1.5, 2.5))
METALS = [
    ("Superconductor (NbTi, 0 Ω reference)", "Superconductor", 0.0,       "#e8e8e8", DOT),
    ("Silver — Ohno Continuous Cast 7N",     "Silver OCC 7N",  15.87e-9,  "#a7b4c2", SOLID),
    ("Copper — Grade 1 6N OFC",              "Copper 6N OFC",  16.78e-9,  "#e07a3f", SOLID),
    ("Gold — pure",                          "Gold",           22.14e-9,  "#f2c230", SOLID),
    ("Rhodium",                              "Rhodium",        43.0e-9,   "#7fd6e8", DASH),
    ("Iridium",                              "Iridium",        49.0e-9,   "#9b7bea", DASH),
    ("Platinum-Iridium 10",                  "Pt-Ir 10",       250.0e-9,  "#e3a8f0", DASHDOT),
    ("Monel 400",                            "Monel 400",      0.53e-6,   "#6cc04a", SOLID),
    ("Haynes 188",                           "Haynes 188",     1.01e-6,   "#12c9b8", SOLID),
    ("MP35N",                                "MP35N",          1.03e-6,   "#5a8cff", DASH),
    ("Waspaloy",                             "Waspaloy",       1.22e-6,   "#e8559b", SOLID),
    ("Inconel 718",                          "Inconel 718",    1.24e-6,   "#ef4444", SOLID),
    ("Hastelloy C-276",                      "Hastelloy C-276",1.26e-6,   "#c8e04a", DASH),
    ("Inconel 625",                          "Inconel 625",    1.29e-6,   "#ff9db0", SOLID),
    ("René 41",                              "René 41",        1.29e-6,   "#2f6bff", DASH),
]
NM = len(METALS)
COPPER = 2

# ---------------------------------------------------------------- style
SURFACE, PANEL = "#1a1a19", "#141413"
GRID, GRID_MAJOR = "#2b2a27", "#3a3935"
TEXT, TEXT_2, TEXT_3 = "#f0efec", "#c3c2b7", "#8f8e86"
BAND = "#1f1f1d"
for fam in ("Inter", "DejaVu Sans"):
    if any(fam == f.name for f in font_manager.fontManager.ttflist):
        plt.rcParams["font.family"] = fam
        break
plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": PANEL, "axes.edgecolor": GRID_MAJOR,
    "axes.labelcolor": TEXT_2, "xtick.color": TEXT_3, "ytick.color": TEXT_3,
    "text.color": TEXT, "font.size": 11, "axes.titlecolor": TEXT, "axes.titlesize": 13,
})
LW = 1.9


def hz(v, _p=None):
    return f"{v / 1000:g}k" if v >= 1000 else f"{v:g}"


def signed(v, nd):
    r = round(v, nd)
    return f"{0:.{nd}f}" if r == 0 else f"{r:+.{nd}f}"


def figure(title, subtitle, n_rows=1, height_ratios=None, legend=True, width=15, height=8.8):
    fig = plt.figure(figsize=(width, height), dpi=150)
    right = 0.835 if legend else 0.965
    gs = fig.add_gridspec(n_rows, 1, height_ratios=height_ratios, hspace=0.16,
                          left=0.065, right=right, top=0.835, bottom=0.10)
    fig.text(0.065, 0.950, "SUPERALLOY  ·  ANALYSIS", color=TEXT_3, fontsize=10.5, fontweight="bold")
    fig.text(0.065, 0.900, title, color=TEXT, fontsize=23, fontweight="bold")
    fig.text(0.065, 0.862, subtitle, color=TEXT_2, fontsize=11.5)
    axes = [fig.add_subplot(gs[i]) for i in range(n_rows)]
    if legend:
        legend_column(fig, right + 0.012)
    return fig, axes


def legend_column(fig, x0, metals=range(NM)):
    lax = fig.add_axes([x0, 0.10, 0.99 - x0, 0.735])
    lax.axis("off")
    ms = list(metals)
    for i, m in enumerate(ms):
        _, label, _, col, ls = METALS[m]
        y = 1 - (i + 0.5) / len(ms)
        lax.add_line(Line2D([0.0, 0.17], [y, y], color=col, lw=2.4, ls=ls, transform=lax.transAxes))
        lax.text(0.22, y, label, color=TEXT_2, fontsize=10, va="center", transform=lax.transAxes)


def footer(fig, text):
    fig.text(0.065, 0.025, text, color=TEXT_3, fontsize=9.5)


def style_axes(ax):
    for s in ax.spines.values():
        s.set_visible(False)
    ax.tick_params(length=0, pad=6)
    ax.grid(which="major", color=GRID_MAJOR, linewidth=0.8)
    ax.grid(which="minor", color=GRID, linewidth=0.45)


def freq_axis(ax, fmin, fmax, shade=True, labels=True):
    ax.set_xscale("log")
    ax.set_xlim(fmin, fmax)
    majors = [v for v in (10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000)
              if fmin <= v <= fmax]
    ax.xaxis.set_major_locator(FixedLocator(majors))
    ax.xaxis.set_major_formatter(FuncFormatter(hz) if labels else NullFormatter())
    minors = [m * d for d in (1, 10, 100, 1000, 10000, 100000) for m in range(2, 10)
              if fmin <= m * d <= fmax and m * d not in majors]
    ax.xaxis.set_minor_locator(FixedLocator(minors))
    ax.xaxis.set_minor_formatter(NullFormatter())
    if shade:
        if fmin < 20:
            ax.axvspan(fmin, 20, color=BAND, zorder=0, lw=0)
        if fmax > 20000:
            ax.axvspan(20000, fmax, color=BAND, zorder=0, lw=0)
    style_axes(ax)


def draw(ax, x, y, m, **kw):
    _, label, _, col, ls = METALS[m]
    z = 3 + (m % 5) * 0.01
    ax.plot(x, y, color=col, ls=ls, lw=kw.pop("lw", LW), zorder=kw.pop("zorder", z), label=label, **kw)


# ---------------------------------------------------------------- data
def load(resp_csv, adir):
    r = pd.read_csv(resp_csv)
    d = dict(
        fr=r,
        thdf=pd.read_csv(os.path.join(adir, "thd_freq.csv")),
        thdl=pd.read_csv(os.path.join(adir, "thd_level.csv")),
        imd=pd.read_csv(os.path.join(adir, "imd.csv")),
        step=pd.read_csv(os.path.join(adir, "step.csv")),
        null=pd.read_csv(os.path.join(adir, "null.csv")),
    )
    return d


def fr(d, m, level=-18.0):
    s = d["fr"][(d["fr"].metal == m) & (d["fr"].level_dbfs == level)].sort_values("freq_hz")
    s = s.drop_duplicates(subset="freq_hz")
    return s.freq_hz.values, s.mag_db.values, s.phase_deg.values


def interp_log(x, y, xq):
    return np.interp(np.log10(xq), np.log10(x), y)


def group_delay(f, phase_deg):
    w = 2 * np.pi * f
    ph = np.unwrap(np.radians(phase_deg))
    return -np.gradient(ph, w)            # seconds


# ---------------------------------------------------------------- graphs
def g_magnitude(d, out, fs):
    fig, (ax,) = figure("Magnitude response", f"All metals at −18 dBFS  ·  {fs/1000:g} kHz  ·  0 dB = input level")
    f0 = None
    for m in range(NM):
        f, mag, _ = fr(d, m)
        draw(ax, f, mag, m)
        f0 = f
    freq_axis(ax, 10, f0[-1])
    ax.set_ylim(-0.25, 0.03)
    ax.yaxis.set_major_locator(MultipleLocator(0.05))
    ax.yaxis.set_minor_locator(MultipleLocator(0.01))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: signed(v, 2) if abs(v) > 1e-9 else "0"))
    ax.set_ylabel("Magnitude (dB)")
    ax.set_xlabel("Frequency (Hz)")
    ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    footer(fig, "Shaded: outside 20 Hz – 20 kHz.  Inconel 625 and René 41 have the same published resistivity, so their traces coincide (René 41 dashed on top).")
    fig.savefig(os.path.join(out, "01_magnitude_response.png"), facecolor=SURFACE)
    plt.close(fig)


def g_dev_copper(d, out, fs):
    fig, (ax,) = figure("Deviation from copper",
                        f"Magnitude of each metal minus copper, −18 dBFS  ·  {fs/1000:g} kHz  ·  your shape cap is ±0.1 dB from 20 Hz to 20 kHz")
    fc, mc, _ = fr(d, COPPER)
    for m in range(NM):
        f, mag, _ = fr(d, m)
        draw(ax, f, mag - mc, m)
    freq_axis(ax, 10, fc[-1])
    ax.axhspan(-0.1, 0.1, xmin=0, xmax=1, color="#24302a", zorder=0.5, lw=0)
    for y in (-0.1, 0.1):
        ax.axhline(y, color="#5b7a66", lw=1.0, ls=(0, (3, 3)), zorder=1)
    ax.text(25, 0.1, "  +0.1 dB cap", color="#8fb39c", fontsize=9.5, va="bottom")
    ax.text(25, -0.1, "  −0.1 dB cap", color="#8fb39c", fontsize=9.5, va="top")
    ax.set_ylim(-0.25, 0.13)
    ax.yaxis.set_major_locator(MultipleLocator(0.05))
    ax.yaxis.set_minor_locator(MultipleLocator(0.01))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: signed(v, 2) if abs(v) > 1e-9 else "0"))
    ax.set_ylabel("Δ magnitude vs copper (dB)")
    ax.set_xlabel("Frequency (Hz)")
    ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    footer(fig, "Above 20 kHz the low-resistivity metals roll off (skin effect) and the superalloys do not, so they sit above copper there.")
    fig.savefig(os.path.join(out, "02_deviation_vs_copper.png"), facecolor=SURFACE)
    plt.close(fig)


def g_dev_zoom(d, out, fs):
    """Two zoomed windows: deep bass and the top octave + ultrasonic."""
    fig, axes = figure("Deviation zoom", f"Magnitude vs input, −18 dBFS  ·  {fs/1000:g} kHz  ·  left: 10 – 200 Hz   right: 5 kHz – Nyquist",
                       n_rows=1)
    fig.delaxes(axes[0])
    gs = fig.add_gridspec(1, 2, left=0.065, right=0.835, top=0.835, bottom=0.10, wspace=0.14)
    axl, axr = fig.add_subplot(gs[0]), fig.add_subplot(gs[1])
    f0 = None
    for m in range(NM):
        f, mag, _ = fr(d, m)
        draw(axl, f, mag, m)
        draw(axr, f, mag, m)
        f0 = f
    freq_axis(axl, 10, 200, shade=True)
    freq_axis(axr, 5000, f0[-1], shade=True)
    axl.set_ylim(-0.24, 0.005)
    axr.set_ylim(-0.03, 0.002)
    axl.yaxis.set_major_locator(MultipleLocator(0.02))
    axr.yaxis.set_major_locator(MultipleLocator(0.005))
    axl.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: signed(v, 2) if abs(v) > 1e-9 else "0"))
    axr.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: signed(v, 3) if abs(v) > 1e-9 else "0"))
    axl.set_ylabel("Magnitude (dB)")
    for a in (axl, axr):
        a.set_xlabel("Frequency (Hz)")
        a.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    footer(fig, "Different vertical scales left and right: the deep-bass window spans 0.24 dB, the top window 0.03 dB.")
    fig.savefig(os.path.join(out, "03_deviation_zoom_bass_and_top.png"), facecolor=SURFACE)
    plt.close(fig)


def g_heatmap(d, out, fs):
    centres = 1000.0 * 2.0 ** (np.arange(-17, 23) / 3.0)          # 20 Hz … 160 kHz
    f, _, _ = fr(d, 0)
    centres = centres[centres <= f[-1]]
    M = np.zeros((NM, len(centres)))
    for m in range(NM):
        fm, mag, _ = fr(d, m)
        M[m] = interp_log(fm, mag, centres)
    lim = 0.065
    cmap = LinearSegmentedColormap.from_list("div", ["#2a78d6", "#5598e7", "#9ec5f4", "#383835", "#f4a7a6", "#e66767", "#c43b3a"])
    fig, (ax,) = figure("Deviation map", f"Magnitude vs input per 1/3-octave band, −18 dBFS  ·  {fs/1000:g} kHz  ·  blue = cut, red = boost, grey = flat",
                        legend=False)
    ax.set_position([0.115, 0.10, 0.80, 0.735])
    im = ax.imshow(M, aspect="auto", cmap=cmap, norm=TwoSlopeNorm(vcenter=0.0, vmin=-lim, vmax=lim),
                   interpolation="nearest")
    ax.set_yticks(range(NM))
    ax.set_yticklabels([mt[1] for mt in METALS])
    ax.set_xticks(range(len(centres)))
    nominal = ["20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800",
               "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k", "25k",
               "31.5k", "40k", "50k", "63k", "80k", "100k", "125k", "160k"]
    ax.set_xticklabels([nominal[i] if i % 2 == 0 else "" for i in range(len(centres))], rotation=0, fontsize=9)
    ax.set_xlabel("1/3-octave band centre (Hz)")
    for s in ax.spines.values():
        s.set_visible(False)
    ax.tick_params(length=0, pad=5)
    ax.set_xticks(np.arange(-0.5, len(centres), 1), minor=True)
    ax.set_yticks(np.arange(-0.5, NM, 1), minor=True)
    ax.grid(which="minor", color=SURFACE, linewidth=2)
    ax.grid(which="major", visible=False)
    k20 = int(np.argmin(np.abs(centres - 20000)))
    ax.axvline(k20 + 0.5, color=TEXT_3, lw=1.0, ls=(0, (3, 3)))
    ax.text(k20 + 0.6, -0.8, "20 kHz →", color=TEXT_3, fontsize=9.5, va="bottom")
    for m in range(NM):                                     # annotate each row's worst band
        k = int(np.argmax(np.abs(M[m])))
        if abs(M[m, k]) >= 0.0005:
            ax.text(k, m, f"{M[m, k]:+.3f}".replace("0.", "."), ha="center", va="center", fontsize=7.5,
                    color=TEXT if abs(M[m, k]) > 0.03 else "#111")
    cax = fig.add_axes([0.928, 0.10, 0.014, 0.735])
    cb = fig.colorbar(im, cax=cax)
    cb.set_label("dB", color=TEXT_2)
    cb.ax.tick_params(colors=TEXT_3, length=0)
    cb.outline.set_visible(False)
    footer(fig, "Each row's worst band is printed in its cell.  Bands from 20 Hz; the 10 Hz point is outside this map (see the zoom graph).")
    fig.savefig(os.path.join(out, "04_deviation_map_third_octave.png"), facecolor=SURFACE)
    plt.close(fig)


def g_phase(d, out, fs):
    fig, (ax,) = figure("Phase response", f"All metals at −18 dBFS  ·  {fs/1000:g} kHz  ·  positive = phase lead")
    f0 = None
    for m in range(NM):
        f, _, ph = fr(d, m)
        draw(ax, f, ph, m)
        f0 = f
    freq_axis(ax, 10, f0[-1])
    ax.set_ylim(-1, 17)
    ax.yaxis.set_major_locator(MultipleLocator(2))
    ax.yaxis.set_minor_locator(MultipleLocator(0.5))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}°" if v else "0°"))
    ax.set_ylabel("Phase")
    ax.set_xlabel("Frequency (Hz)")
    ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    footer(fig, "The superalloys' bass phase lead comes from their higher winding resistance raising the transformer's low-frequency corner to about 2.4 Hz.")
    fig.savefig(os.path.join(out, "05_phase_response.png"), facecolor=SURFACE)
    plt.close(fig)


def g_phase_dev(d, out, fs):
    fig, (ax,) = figure("Phase deviation from copper", f"Phase of each metal minus copper, −18 dBFS  ·  {fs/1000:g} kHz")
    fc, _, pc = fr(d, COPPER)
    for m in range(NM):
        f, _, ph = fr(d, m)
        draw(ax, f, ph - pc, m)
    freq_axis(ax, 10, fc[-1])
    ax.set_yscale("symlog", linthresh=0.01, linscale=0.6)
    ax.set_ylim(-0.4, 20)
    ticks = [-0.1, -0.01, 0, 0.01, 0.1, 1, 10]
    ax.yaxis.set_major_locator(FixedLocator(ticks))
    ax.yaxis.set_minor_locator(FixedLocator([]))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}°" if v else "0°"))
    ax.set_ylabel("Δ phase vs copper (log-scaled)")
    ax.set_xlabel("Frequency (Hz)")
    ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    footer(fig, "Vertical axis is logarithmic above ±0.01° and linear inside it, so both the 15° bass lead and the 0.01° treble differences are readable.")
    fig.savefig(os.path.join(out, "06_phase_deviation_vs_copper.png"), facecolor=SURFACE)
    plt.close(fig)


def g_group_delay(d, out, fs):
    fig, (ax,) = figure("Group delay", f"−dφ/dω from the measured phase, −18 dBFS  ·  {fs/1000:g} kHz")
    f0 = None
    for m in range(NM):
        f, _, ph = fr(d, m)
        gd = group_delay(f, ph) * 1e6          # µs
        draw(ax, f, np.where(gd > 1e-3, gd, np.nan), m)
        f0 = f
    freq_axis(ax, 10, f0[-1])
    ax.set_yscale("log")
    ax.set_ylim(1e-3, 5e3)
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: (f"{v/1000:g} ms" if v >= 1000 else f"{v:g} µs")))
    ax.set_ylabel("Group delay (log)")
    ax.set_xlabel("Frequency (Hz)")
    footer(fig, "Logarithmic scale; a trace ends where its group delay drops below 0.001 µs.  The plugin has zero latency: this is the frequency-dependent part only.  "
                "Superconductor: the 0.01 Hz drive DC blocker.")
    fig.savefig(os.path.join(out, "07_group_delay.png"), facecolor=SURFACE)
    plt.close(fig)


def g_level_dependence(d, out, fs):
    fig, (ax,) = figure("Level dependence of the response",
                        f"Magnitude at 0 dBFS minus magnitude at −18 dBFS  ·  {fs/1000:g} kHz  ·  positive = louder input keeps more")
    f0 = None
    for m in range(NM):
        f, a, _ = fr(d, m, -18.0)
        _, b, _ = fr(d, m, 0.0)
        draw(ax, f, b - a, m)
        f0 = f
    freq_axis(ax, 10, f0[-1])
    ax.set_ylim(-0.01, 0.15)
    ax.yaxis.set_major_locator(MultipleLocator(0.02))
    ax.yaxis.set_minor_locator(MultipleLocator(0.005))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: signed(v, 2) if abs(v) > 1e-9 else "0"))
    ax.set_ylabel("Δ magnitude, 0 dBFS − (−18 dBFS) (dB)")
    ax.set_xlabel("Frequency (Hz)")
    ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    footer(fig, "The modelled core inductance rises with flux, so hotter material loses less deep bass. This is a level-dependent (dynamic) effect in the bottom octave.")
    fig.savefig(os.path.join(out, "08_level_dependence.png"), facecolor=SURFACE)
    plt.close(fig)


FLOOR = -200.0


def g_thd_freq(d, out, fs):
    t = d["thdf"]
    fig, axes = figure("Harmonic distortion vs frequency", f"THD (H2–H9) re fundamental  ·  {fs/1000:g} kHz  ·  top: 0 dBFS   bottom: −18 dBFS",
                       n_rows=2, height_ratios=[1, 1])
    for ax, lv in zip(axes, (0.0, -18.0)):
        for m in range(NM):
            s = t[(t.metal == m) & (t.level_dbfs == lv)].sort_values("freq_hz")
            draw(ax, s.freq_hz.values, np.maximum(s.thd_db.values, FLOOR), m)
        freq_axis(ax, 10, 10000, labels=(lv == -18.0))
        ax.set_ylim(FLOOR, -20)
        ax.yaxis.set_major_locator(MultipleLocator(20))
        ax.yaxis.set_minor_locator(MultipleLocator(10))
        ax.set_ylabel(f"THD at {signed(lv,0) if lv else '0'} dBFS (dB)")
        pct = {-40: "1%", -60: "0.1%", -80: "0.01%", -100: "0.001%", -120: "0.0001%"}
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:g}  ({pct[int(v)]})" if int(v) in pct else f"{v:g}"))
    for ax in axes:
        pos = ax.get_position()
        ax.set_position([0.125, pos.y0, 0.835 - 0.125, pos.height])
    axes[1].set_xlabel("Fundamental frequency (Hz)")
    footer(fig, "Harmonics measured up to H9 or 0.45·fs.  −200 dB is the drawing floor (the superconductor sits below it).  Bracketed labels give the same values in %.")
    fig.savefig(os.path.join(out, "09_thd_vs_frequency.png"), facecolor=SURFACE)
    plt.close(fig)


def g_thd_level(d, out, fs):
    t = d["thdl"]
    fig, axes = figure("Harmonic distortion vs level", f"THD (H2–H9) re fundamental  ·  {fs/1000:g} kHz  ·  top: 20 Hz   bottom: 100 Hz",
                       n_rows=2, height_ratios=[1, 1])
    for ax, fq in zip(axes, (20.0, 100.0)):
        for m in range(NM):
            s = t[(t.metal == m) & (t.freq_hz == fq)].sort_values("level_dbfs")
            draw(ax, s.level_dbfs.values, np.maximum(s.thd_db.values, FLOOR), m)
        ax.set_xlim(-40, 36)
        ax.xaxis.set_major_locator(MultipleLocator(6))
        ax.xaxis.set_minor_locator(MultipleLocator(2))
        if fq == 20.0:
            ax.xaxis.set_major_formatter(NullFormatter())
        else:
            ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}" if v else "0"))
        ax.set_ylim(FLOOR, 0)
        ax.yaxis.set_major_locator(MultipleLocator(20))
        style_axes(ax)
        ax.axvspan(0, 36, color=BAND, zorder=0, lw=0)
        ax.axvline(18, color="#8a6d3b", lw=1.1, ls=(0, (3, 3)), zorder=1)
        ax.text(17.6, -192, "+18 dBFS design clip point", color="#c9a46a", fontsize=9.5, va="bottom", ha="right")
        ax.set_ylabel(f"THD at {fq:g} Hz (dB)")
    axes[1].set_xlabel("Input level (dBFS)")
    footer(fig, "Shaded: above 0 dBFS (only reachable with floating-point input).  Distortion falls toward +18 dBFS as the core moves past its knee, then rises steeply into saturation.")
    fig.savefig(os.path.join(out, "10_thd_vs_level.png"), facecolor=SURFACE)
    plt.close(fig)


def g_compression(d, out, fs):
    t = d["thdl"]
    fig, axes = figure("Level linearity and saturation", f"Fundamental gain vs input level  ·  {fs/1000:g} kHz  ·  top: 20 Hz   bottom: 100 Hz",
                       n_rows=2, height_ratios=[1, 1])
    for ax, fq in zip(axes, (20.0, 100.0)):
        for m in range(NM):
            s = t[(t.metal == m) & (t.freq_hz == fq)].sort_values("level_dbfs")
            g0 = s.gain_db.values[0]
            draw(ax, s.level_dbfs.values, s.gain_db.values - g0, m)
        ax.set_xlim(-40, 36)
        ax.xaxis.set_major_locator(MultipleLocator(6))
        if fq == 20.0:
            ax.xaxis.set_major_formatter(NullFormatter())
        else:
            ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}" if v else "0"))
        ax.set_ylim(-9, 0.5)
        ax.yaxis.set_major_locator(MultipleLocator(1))
        style_axes(ax)
        ax.axvspan(0, 36, color=BAND, zorder=0, lw=0)
        ax.axhline(-1, color="#8a6d3b", lw=1.0, ls=(0, (3, 3)), zorder=1)
        ax.text(-39, -1.08, "1 dB compression", color="#c9a46a", fontsize=9.5, va="top")
        ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
        ax.set_ylabel(f"Gain change at {fq:g} Hz (dB)")
    axes[1].set_xlabel("Input level (dBFS)")
    footer(fig, "Gain relative to the same metal at −40 dBFS.  20 Hz is the worst case (core flux is proportional to voltage ÷ frequency); at 100 Hz no metal compresses even at +36 dBFS.")
    fig.savefig(os.path.join(out, "11_compression_saturation.png"), facecolor=SURFACE)
    plt.close(fig)


def g_harmonics(d, out, fs):
    t = d["thdl"]
    fig, axes = figure("Harmonic profile", f"Each harmonic re fundamental at 20 Hz  ·  {fs/1000:g} kHz  ·  top: 0 dBFS   bottom: +18 dBFS",
                       n_rows=2, height_ratios=[1, 1])
    hcols = [f"h{k}_db" for k in range(2, 10)]
    for ax, lv in zip(axes, (0.0, 18.0)):
        s = t[(t.freq_hz == 20.0) & (t.level_dbfs == lv)]
        for m in range(NM):
            row = s[s.metal == m]
            if row.empty:
                continue
            y = np.maximum(row[hcols].values[0].astype(float), FLOOR)
            x = np.arange(2, 10) + (m - NM / 2) * 0.022
            _, _, _, col, ls = METALS[m]
            ax.plot(x, y, color=col, ls=ls, lw=1.4, marker="o", markersize=5.5,
                    markeredgecolor=PANEL, markeredgewidth=1.2, zorder=3)
        ax.set_xlim(1.5, 9.5)
        ax.xaxis.set_major_locator(FixedLocator(range(2, 10)))
        ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"H{int(v)}") if lv == 18.0 else NullFormatter())
        ax.set_ylim(FLOOR, 0)
        ax.yaxis.set_major_locator(MultipleLocator(20))
        style_axes(ax)
        ax.set_ylabel(f"{signed(lv, 0) if lv else '0'} dBFS (dB re H1)")
    axes[1].set_xlabel("Harmonic")
    footer(fig, "The core is symmetric, so in steady state it makes odd harmonics only. The even ones here are left over from starting each test tone: "
                "with 48 s more settling they fall a further ~30 dB while the odd ones do not move.")
    fig.savefig(os.path.join(out, "12_harmonic_profile_20Hz.png"), facecolor=SURFACE)
    plt.close(fig)


def g_imd(d, out, fs):
    t = d["imd"]
    fig, (ax,) = figure("Intermodulation distortion", f"SMPTE (60 Hz + 7 kHz, 4:1), sidebands n = 1–4 re the 7 kHz carrier  ·  {fs/1000:g} kHz")
    for m in range(NM):
        s = t[t.metal == m].sort_values("peak_dbfs")
        draw(ax, s.peak_dbfs.values, np.maximum(s.imd_db.values, FLOOR), m)
    ax.set_xlim(-30, 12)
    ax.xaxis.set_major_locator(MultipleLocator(3))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:+g}" if v else "0"))
    ax.set_ylim(FLOOR, -40)
    ax.yaxis.set_major_locator(MultipleLocator(20))
    style_axes(ax)
    ax.axvspan(0, 12, color=BAND, zorder=0, lw=0)
    ax.set_ylabel("IMD (dB)")
    ax.set_xlabel("Combined peak level (dBFS)")
    footer(fig, "Low IMD across the board: the 60 Hz tone modulates the core's inductance, which barely affects a 7 kHz signal.")
    fig.savefig(os.path.join(out, "13_imd_smpte.png"), facecolor=SURFACE)
    plt.close(fig)


def g_step(d, out, fs):
    t = d["step"]
    fig, (ax,) = figure("Step response", f"−18 dBFS step at 10 ms, output ÷ step height  ·  {fs/1000:g} kHz")
    for m in range(NM):
        s = t[t.metal == m]
        draw(ax, s.t_s.values * 1000, s.y.values, m)
    ax.set_xlim(0, 600)
    ax.xaxis.set_major_locator(MultipleLocator(50))
    ax.xaxis.set_minor_locator(MultipleLocator(10))
    ax.set_ylim(-0.15, 1.1)
    ax.yaxis.set_major_locator(MultipleLocator(0.1))
    style_axes(ax)
    ax.axhline(0, color=TEXT_3, lw=0.9, zorder=1)
    ax.axhline(1, color=TEXT_3, lw=0.7, ls=(0, (3, 3)), zorder=1)
    ax.set_ylabel("Output ÷ step")
    ax.set_xlabel("Time (ms)")
    footer(fig, "A transformer cannot pass DC, so every step decays. The superalloys decay within ~100 ms (corner ≈ 2.4 Hz); the superconductor's slow fall is the 0.01 Hz drive DC blocker.")
    fig.savefig(os.path.join(out, "14_step_response.png"), facecolor=SURFACE)
    plt.close(fig)


def g_null_spectrum(d, out, fs):
    t = d["null"]
    fig, (ax,) = figure("Programme null vs superconductor",
                        f"Pink noise at −16 dBFS RMS: 1/3-octave level of (metal output − superconductor output), re programme  ·  {fs/1000:g} kHz",
                        legend=False)
    legend_column(fig, 0.835 + 0.012, metals=range(1, NM))
    fig.axes[0].set_position([0.065, 0.10, 0.77, 0.735])
    for m in range(1, NM):
        s = t[(t.metal == m) & (t.band_hz > 0)].sort_values("band_hz")
        draw(ax, s.band_hz.values, s.null_vs_superconductor_db.values, m, marker="o", markersize=3.5)
    freq_axis(ax, 18, 22000, shade=False)
    ax.set_ylim(-140, -10)
    ax.yaxis.set_major_locator(MultipleLocator(10))
    ax.set_ylabel("Null residual (dB re programme)")
    ax.set_xlabel("1/3-octave band (Hz)")
    footer(fig, "Lower = closer to the ideal 0 Ω winding.  The bass residual is almost entirely the phase lead, not level: 8° of phase alone leaves a −17 dB residual.")
    fig.savefig(os.path.join(out, "15_null_spectrum_vs_superconductor.png"), facecolor=SURFACE)
    plt.close(fig)


def g_null_bars(d, out, fs):
    t = d["null"]
    bb = t[t.band_hz == 0].set_index("metal")
    fig, (ax,) = figure("Null depth, broadband", "Pink noise at −16 dBFS RMS, 20 Hz – 20 kHz: residual of each metal re programme  ·  lower = closer to ideal",
                        legend=False)
    ax.set_position([0.115, 0.10, 0.85, 0.735])
    ms = list(range(1, NM))
    ys = np.arange(len(ms))[::-1]
    for y, m in zip(ys, ms):
        v = bb.loc[m, "null_vs_superconductor_db"]
        vin = bb.loc[m, "null_vs_input_db"]
        _, label, _, col, _ = METALS[m]
        ax.barh(y, v - (-90), left=-90, height=0.62, color=col, zorder=3)
        ax.text(max(v, vin) + 0.8, y, f"{v:.1f} dB", va="center", ha="left", color=TEXT, fontsize=10, zorder=4)
        ax.plot([vin], [y], marker="|", markersize=16, mew=2, color=TEXT_2, zorder=5)
    ax.set_yticks(ys)
    ax.set_yticklabels([METALS[m][1] for m in ms])
    ax.set_xlim(-90, -20)
    ax.xaxis.set_major_locator(MultipleLocator(10))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:g} dB"))
    style_axes(ax)
    ax.grid(axis="y", visible=False)
    ax.set_xlabel("Residual (dB re programme)")
    sc = bb.loc[0, "null_vs_input_db"]
    footer(fig, f"Bar: null vs superconductor (isolates the metal).  Tick: null vs dry input (adds the shared 0.01 Hz drive DC blocker; the superconductor alone measures {sc:.1f} dB).")
    fig.savefig(os.path.join(out, "16_null_depth_broadband.png"), facecolor=SURFACE)
    plt.close(fig)


def scorecard(d, out, fs):
    fc, mc, pc = fr(d, COPPER)
    t, L, imd, nl = d["thdf"], d["thdl"], d["imd"], d["null"]
    rows = []
    for m in range(NM):
        f, mag, ph = fr(d, m)
        band = (f >= 20) & (f <= 20000)
        dev = mag[band] - mc[band]
        worst = dev[np.argmax(np.abs(dev))]
        rms = math.sqrt(float(np.mean(dev ** 2)))
        m20 = interp_log(f, mag, 20.0)
        p20 = interp_log(f, ph, 20.0)
        gd20 = interp_log(f, group_delay(f, ph), 20.0) * 1000
        # −0.01 dB LF point
        below = np.where(mag < -0.01)[0]
        lf = below[f[below] < 1000]
        lfpt = "< 10 Hz" if len(lf) == 0 else f"{f[lf.max() + 1]:.0f} Hz"
        def thd_at(lv, fq):
            s = t[(t.metal == m) & (t.level_dbfs == lv)]
            v = s.iloc[(s.freq_hz - fq).abs().argsort()[:1]].thd_db.values[0]
            return v
        th20, th100 = thd_at(0.0, 20.0), thd_at(0.0, 100.0)
        s = L[(L.metal == m) & (L.freq_hz == 20.0)].sort_values("level_dbfs")
        g = s.gain_db.values - s.gain_db.values[0]
        lv = s.level_dbfs.values
        idx = np.where(g <= -1.0)[0]
        if len(idx) == 0:
            comp = "> +36 dBFS"
        else:
            i = idx[0]
            x = lv[i - 1] + (lv[i] - lv[i - 1]) * (-1.0 - g[i - 1]) / (g[i] - g[i - 1])
            comp = f"{x:+.1f} dBFS"
        im0 = imd[(imd.metal == m) & (imd.peak_dbfs == 0.0)].imd_db.values[0]
        nsc = nl[(nl.metal == m) & (nl.band_hz == 0)].null_vs_superconductor_db.values[0]
        def pct(db):
            return "—" if db < -190 else f"{100 * 10 ** (db / 20):.3g}%"
        rho = METALS[m][2]
        rows.append([
            METALS[m][1],
            "0" if rho == 0 else (f"{rho * 1e9:.2f} nΩ·m" if rho < 1e-6 else f"{rho * 1e6:.2f} µΩ·m"),
            signed(m20, 4),
            "ref" if m == COPPER else signed(worst, 4),
            "ref" if m == COPPER else f"{rms:.4f}",
            lfpt,
            f"{p20:+.2f}°",
            f"{gd20:.3f} ms",
            pct(th20), pct(th100),
            "—" if im0 < -190 else f"{im0:.1f} dB",
            comp,
            "ref" if m == 0 else f"{nsc:.1f} dB",
        ])
    cols = ["Metal", "Resistivity", "20 Hz\n(dB)", "Worst Δ vs\ncopper (dB)", "RMS Δ vs\ncopper (dB)", "−0.01 dB\nLF point",
            "Phase\n@ 20 Hz", "Group delay\n@ 20 Hz", "THD 20 Hz\n0 dBFS", "THD 100 Hz\n0 dBFS", "IMD SMPTE\n0 dBFS",
            "1 dB comp.\n@ 20 Hz", "Null vs\nsupercond."]
    fig = plt.figure(figsize=(18, 9.6), dpi=150)
    fig.text(0.03, 0.950, "SUPERALLOY  ·  ANALYSIS", color=TEXT_3, fontsize=10.5, fontweight="bold")
    fig.text(0.03, 0.905, "Scorecard", color=TEXT, fontsize=23, fontweight="bold")
    fig.text(0.03, 0.870, f"Every number measured from the plugin processor at {fs/1000:g} kHz.  Response figures at −18 dBFS; deviation columns over 20 Hz – 20 kHz.",
             color=TEXT_2, fontsize=11.5)
    ax = fig.add_axes([0.03, 0.07, 0.94, 0.77])
    ax.axis("off")
    ncol = len(cols)
    widths = np.array([1.55, 1.15, 0.85, 1.0, 0.95, 0.85, 0.8, 0.95, 0.9, 0.9, 0.95, 1.0, 0.95])
    xs = np.concatenate([[0], np.cumsum(widths)]) / widths.sum()
    nrow = len(rows) + 1
    rh = 1.0 / nrow
    for j, c in enumerate(cols):
        ax.text(xs[j] + 0.004 if j == 0 else xs[j + 1] - 0.006, 1 - rh * 0.5, c, ha="left" if j == 0 else "right",
                va="center", color=TEXT_3, fontsize=9.5, fontweight="bold", transform=ax.transAxes)
    for i, r in enumerate(rows):
        y = 1 - rh * (i + 1.5)
        if i % 2 == 0:
            ax.add_patch(plt.Rectangle((0, y - rh / 2), 1, rh, color="#20201e", transform=ax.transAxes, zorder=0, lw=0))
        _, _, _, col, ls = METALS[i]
        ax.add_line(Line2D([0.002, 0.030], [y, y], color=col, lw=2.6, ls=ls, transform=ax.transAxes))
        for j, v in enumerate(r):
            if j == 0:
                ax.text(0.036, y, v, ha="left", va="center", color=TEXT, fontsize=10.5, transform=ax.transAxes)
            else:
                ax.text(xs[j + 1] - 0.006, y, v, ha="right", va="center",
                        color=TEXT if v not in ("ref", "—") else TEXT_3, fontsize=10.5, transform=ax.transAxes,
                        family="DejaVu Sans Mono" if any(ch.isdigit() for ch in v) else None)
    ax.axhline(1 - rh, color=GRID_MAJOR, lw=1)
    fig.text(0.03, 0.03, "—: below the −190 dB measurement floor.   RMS Δ: root-mean-square of the deviation on the log-frequency grid.   "
             "1 dB comp.: input level where the 20 Hz gain has fallen 1 dB.", color=TEXT_3, fontsize=9.5)
    fig.savefig(os.path.join(out, "00_scorecard.png"), facecolor=SURFACE)
    plt.close(fig)


def main():
    resp, adir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    fs = float(sys.argv[4]) if len(sys.argv) > 4 else 352800.0
    os.makedirs(out, exist_ok=True)
    d = load(resp, adir)
    for g in (scorecard, g_magnitude, g_dev_copper, g_dev_zoom, g_heatmap, g_phase, g_phase_dev, g_group_delay,
              g_level_dependence, g_thd_freq, g_thd_level, g_compression, g_harmonics, g_imd, g_step,
              g_null_spectrum, g_null_bars):
        g(d, out, fs)
        print("rendered", g.__name__)


if __name__ == "__main__":
    main()
