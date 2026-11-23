# Superalloy — rev 6.4

One metal at a time, wound into a step-up → 630 V → step-down transformer pair.
Medium heat-tinted Inconel rectangle with one dropdown, **Metal**, a four-position switch, **Contour** (Subtle / Color / Curie / Geiger), and a **Bypass** switch (Off / On) in the top-right corner. Zero latency.

* **Subtle** is the rev 6.1 sound, bit for bit (0 of 2.38 million samples differ against the rev 6.1 build, including metal switches, at 44.1 and 352.8 kHz).
* **Color** is the same transformer with an amplified geometry: a core with 1/3.9 of the magnetising inductance and a 2-layer, 2.4 mm foil winding. The metal still enters only through its resistivity. Every metal stays within ±1 dB of flat, and within 1 dB of every other metal, from 20 Hz to 20 kHz. Programme level is matched to within 0.01 dB on pink noise. No distortion mechanism is added. Bit-identical to rev 6.3.
* **Curie** and **Geiger** push the same two variables further: Curie is 3× Color (12× Subtle, cap ±3 dB), Geiger is 12× Color (48× Subtle, cap ±12 dB). The difference between metals measures 2.90 dB and 11.61 dB (3.00× and 12.00× Color's 0.967 dB). Same level match, same zero latency, no added distortion.
* **Bypass** is a true bypass: once its 40 ms crossfade settles, the output is the input bit for bit (float and double). It is the plugin's host bypass parameter, so the DAW's own bypass button and the panel switch are the same control. The engine keeps running underneath, so re-engaging is seamless: the output is identical to an instance that was never bypassed.

(Formerly "wavRely 630". Plugin code `Salo`, manufacturer `Wvry`.)

## Build (Windows 11, Visual Studio 2026)

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DWAVRELY_JUCE_DIR=C:/Dev/JUCE
cmake --build build --config Release --target Superalloy_VST3 verifySuperalloy
build\Release\verifySuperalloy.exe
```

Optional end-to-end check of the built plugin (loads `Superalloy.vst3` in JUCE's VST3 host):

```bat
cmake -S . -B build -DWAVRELY_BUILD_HOSTCHECK=ON
cmake --build build --config Release --target hostcheckSuperalloy
build\hostcheckSuperalloy_artefacts\Release\hostcheckSuperalloy.exe build\Superalloy_artefacts\Release\VST3\Superalloy.vst3
```

* The plugin is written to `build\Superalloy_artefacts\Release\VST3\Superalloy.vst3`. Copy it to `C:\Program Files\Common Files\VST3\`.
* The generator `Visual Studio 18 2026` needs CMake 4.2 or newer.
* `-DWAVRELY_JUCE_DIR` points at your local JUCE 9.0.6 checkout. Leave it out and JUCE 9.0.6 is fetched automatically.
* AAX: add `-DWAVRELY_AAX_SDK=C:/SDKs/aax-sdk`. You then need PACE signing (wraptool) before Pro Tools will load it.
* macOS: built and tested on GitHub Actions (`.github/workflows/macos.yml`): one universal VST3 (Intel + Apple Silicon, macOS 11+), tested on both architectures.
* `verifySuperalloy` must print `RESULT: ALL TESTS PASSED`. Run it after every change to `Source/`.
* The tools switch the Windows console to UTF-8 at start-up, so metal names print correctly (René 41, Copper — Grade 1). The plugin itself is compiled with `/utf-8`, so its GUI is unaffected either way.
* `hostcheckSuperalloy` must print `RESULT: ALL CHECKS PASSED`. It drives the compiled plugin only through its exported parameters, the way a DAW does.
* Optional tools: `-DWAVRELY_BUILD_MEASURE=ON` builds `measureSuperalloy` (frequency response) and `analyzeSuperalloy` (THD, IMD, level, step, programme null); both take `--contour=0..3` (`--color` = 1). `tools/plotAnalysis.py` draws the graphs. `-DWAVRELY_BUILD_DESIGN=ON` builds `designLevels`, which re-derives the Curie and Geiger geometries from Color (`reports/designLevels_report.txt`).

## What is in the signal path

```
DC-free drive (0.01 Hz) → Rp ─┬─ Rs+Rp ─┬─ Rs ─ RL(10 kΩ) → skin shelf → static makeup → CONTOUR ──────────→ BYPASS
                             Lm1        Lm2                                              Subtle: nothing           Off: processed
                       (T1 84% Ni core)  (T2 84% Ni core)                                Color / Curie / Geiger:   On:  input, untouched
                                                                                         per-metal contour
```

In Subtle the metal changes only the winding resistance (∝ ρ) and the skin knee (∝ ρ).

| Element | Value | Origin |
|---|---|---|
| Copper winding R | 9.2 Ω primary / 9.5 Ω secondary | Lundahl LL1517 datasheet |
| Load | 10 kΩ | assumption (bridging mastering input) |
| Magnetising L (initial) | 129.5 H | solved: copper −3 dB at 0.04 Hz (Jensen JT-11SS-DLCF target) |
| Core | 84% Ni: Rayleigh rise → µmax plateau → knee | Whitlock (Jensen) chapter 11; µi/µmax = 0.25 and exponent 6 are assumptions |
| Core distortion constant u_r | 0.0857881 | calibrated: copper 0.002% THD at 20 Hz, +4 dBu (Jensen JT-11-BMCF) |
| Clip point | core knee at +18 dBFS, 20 Hz | owner ruling R6. Narrative: 630 V on the bus at the knee, giving virtual N = 4.566 |
| Skin effect | AWG 18 virtual conductor, exact Bessel solution | owner-approved slight exaggeration |
| Level map | 0 dBFS sine = +24 dBu | assumption (SMPTE RP155) |

### The contour levels

Each level = the Subtle engine (unchanged, nonlinear), followed by a linear contour equal to (level network) ÷ (Subtle network) for the selected metal. Every level changes the same two geometry variables; nothing else changes.

| Level | Core (magnetising inductance) | Foil winding (2.4 mm) | Owner | Difference between metals | vs flat |
|---|---|---|---|---|---|
| Subtle | Lm | round wire (skin effect only) | rev 6.1 | 0.065 dB | — |
| Color | Lm / 3.9 | 2 layers | 4× Subtle, ±1 dB | 0.967 dB | 0.861 dB |
| Curie | Lm / 7.64 | 3.36 layers | 3× Color, ±3 dB | 2.901 dB (2.999× Color) | 2.615 dB |
| Geiger | Lm / 27.6 | 7.52 layers | 12× Color = 48× Subtle, ±12 dB | 11.611 dB (12.001× Color) | 10.653 dB |

How Curie and Geiger were derived (`tools/designLevels.cpp`, report in `reports/designLevels_report.txt`): both mechanisms are scaled by one common factor in dB relative to Color, so Color's balance between bass and treble is kept. The deepest low-frequency contour of any metal at 20 Hz and the deepest high-frequency contour at 20 kHz grow together (3.23× Color for Curie, 14.18× for Geiger). That common factor is solved so that the difference between any two metals is exactly 3× (Curie) and 12× (Geiger) Color's 0.967 dB, which is the figure that set Color's cap. The foil thickness stays 2.4 mm in every level, so each metal's top end starts easing at the same frequency; only the depth grows. The layer count is an effective count: it enters Dowell's solution only as m², so 3.36 is a well-defined winding between 3 and 4 layers, in the same way the virtual turns ratio N = 4.566 is.

| Factor | Physics | Realisation |
|---|---|---|
| Low frequencies | The core's magnetising inductance divided by the level's core factor: both magnetising poles move that many times higher. High-resistivity metals tighten the deep bass; the pure metals barely move. | Exact closed-form pole/zero ratio, two first-order sections |
| Mid and high frequencies | Foil winding: the proximity effect (Dowell's multilayer eddy-current solution) raises each winding's AC impedance where that metal's skin depth falls below the foil thickness. It replaces the Subtle round-wire skin effect. | Color: three first-order sections, the rev 6.2 fit (unchanged). Curie / Geiger: six sections fitted to the **exact** network. At these intensities the foil's internal inductance forms a divider with the reduced magnetising inductance, which the open-core shortcut Color uses would miss by 0.03 dB (Curie) and 0.36 dB (Geiger). All fits are done in the digital domain at every sample rate (bounded Levenberg–Marquardt, analytic Jacobian), and are within 0.0008 dB in band |
| Level | Pink noise band-limited 20 Hz–20 kHz (2nd-order Butterworth), matched to the input level | One gain per metal and level |

All 45 contours (3 levels × 15 metals) run continuously on the engine output. So a metal change or a change of contour position, including a change of mind in the middle of a fade, is a 40 ms equal-gain crossfade between signals that are already settled.

### Bypass

| Property | Behaviour |
|---|---|
| Bypass On, settled | output = input, bit for bit, in float and double. Nothing is added: no gain, no DC block, no dither. |
| Engage / release | 40 ms linear crossfade between the processed and the dry signal. Both are present throughout, so there is no click (measured: the largest sample step inside either fade is below the programme's own largest step). |
| While bypassed | The engine, all 45 contours and the shadow cores keep running on the input. Metal and Contour can be changed while bypassed. When bypass is released, the output is identical to a channel that was never bypassed (difference exactly 0). |
| DAW bypass | `getBypassParameter()` returns the Bypass parameter, so VST3 hosts see it as the `kIsBypass` parameter and AAX maps its bypass to it. The DAW's bypass and the panel switch move together and are automatable. If a host calls `processBlockBypassed`, the same crossfade to the dry signal is used. |
| Recall | Saved in the session state. A session saved bypassed reopens bypassed from its first sample. |
| Latency | 0 samples, bypassed or not, so bypassing never shifts timing. |

### What each metal does in Color (352.8 kHz, −18 dBFS, dB re input)

| Metal | 20 Hz | 5 kHz | 20 kHz | Character |
|---|---|---|---|---|
| Superconductor | 0.00 | 0.00 | 0.00 | identical to Subtle (the 0 Ω reference) |
| Silver / Copper / Gold | +0.10 | −0.13 | −0.38 … −0.48 | full bass, gentle smoothing from ~2 kHz |
| Rhodium / Iridium | +0.11 | −0.09 | −0.66 … −0.71 | smoothing starts ~4 kHz, deepest top |
| Pt-Ir 10 | +0.02 | +0.02 | −0.67 | flat until ~7 kHz, then the top eases |
| Monel 400 | −0.14 | +0.04 | −0.42 | slight bass tightening, eased top |
| Haynes 188 / MP35N | −0.56 | +0.08 | −0.23 | tight bass, open top |
| Waspaloy / Inconel 718 / Hastelloy / Inconel 625 / René 41 | −0.77 … −0.85 | +0.10 | −0.16 … −0.17 | tightest bass, most open top |

Phase at 20 Hz in Color: superalloys +26° to +31° (Subtle: +7° to +8°). Distortion in Color: copper unchanged; superalloys +0.8 dB THD at 20 Hz, 0 dBFS (0.73% → 0.80%, Inconel 718). That is only the contour lowering the 20 Hz fundamental slightly more than its 60 Hz third harmonic. No nonlinearity is added.

### What each metal does in Curie (352.8 kHz, small signal, dB re input)

| Metal | 20 Hz | 50 Hz | 1 kHz | 5 kHz | 20 kHz | 100 kHz | Phase added by the contour, 20 Hz |
|---|---|---|---|---|---|---|---|
| Superconductor | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0° |
| Silver / Copper / Gold | +0.25 … +0.27 | +0.25 … +0.27 | +0.18 … +0.22 | −0.38 … −0.41 | −0.97 … −1.19 | −2.5 … −2.9 | +1° |
| Rhodium / Iridium | +0.30 | +0.30 | +0.27 | −0.29 … −0.32 | −1.81 … −1.95 | −3.9 … −4.1 | +2° |
| Pt-Ir 10 | +0.06 | +0.22 | +0.24 | +0.01 | −2.39 | −8.3 | +12° |
| Monel 400 | −0.51 | +0.13 | +0.26 | +0.09 | −1.88 | −10.0 | +24° |
| Haynes 188 / MP35N | −1.80 … −1.85 | −0.07 | +0.33 | +0.21 | −1.33 … −1.35 | −10.1 | +40° |
| Waspaloy / Inconel 718 / Hastelloy / Inconel 625 / René 41 | −2.39 … −2.59 | −0.19 … −0.23 | +0.37 … +0.39 | +0.26 … +0.28 | −1.12 … −1.17 | −9.8 | +45° … +46° |

### What each metal does in Geiger (352.8 kHz, small signal, dB re input)

| Metal | 20 Hz | 50 Hz | 1 kHz | 5 kHz | 20 kHz | 100 kHz | Phase added by the contour, 20 Hz |
|---|---|---|---|---|---|---|---|
| Superconductor | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0° |
| Silver / Copper / Gold | +0.91 … +0.96 | +0.91 … +0.97 | +0.50 … +0.62 | −1.88 … −2.21 | −3.97 … −4.73 | −8.3 … −9.3 | +3° … +5° |
| Rhodium / Iridium | +0.95 … +0.97 | +1.03 | +0.78 … +0.80 | −2.41 … −2.43 | −6.77 … −7.22 | −11.5 … −11.9 | +9° … +10° |
| Pt-Ir 10 | −0.78 | +0.80 | +1.02 | −1.58 | −10.16 | −18.5 | +44° |
| Monel 400 | −4.02 | +0.04 | +1.30 | −0.98 | −9.55 | −21.0 | +70° |
| Haynes 188 / MP35N | −8.52 … −8.68 | −1.77 … −1.85 | +1.66 | −0.26 … −0.28 | −8.44 … −8.48 | −21.3 | +92° … +93° |
| Waspaloy / Inconel 718 / Hastelloy / Inconel 625 / René 41 | −10.13 … −10.63 | −2.58 … −2.84 | +1.77 … +1.80 | −0.05 … +0.02 | −7.95 … −8.07 | −21.0 | +99° … +100° |

Distortion: no level adds a nonlinearity. The harness proves it: every harmonic H1–H5 of the engine output changes by exactly the contour's own gain at that harmonic's frequency, within 0.024 dB. THD as a percentage still rises on superalloys at 20 Hz (Inconel 718, 0 dBFS: Subtle 0.67 %, Curie 0.88 %, Geiger 1.83 %). That is because Geiger lowers the 20 Hz fundamental by 10.2 dB but its 60 Hz third harmonic by only 1.6 dB. The harmonic itself got quieter; the fundamental got quieter faster.

### Why the MHz parasitics are analytic, not simulated

The composite targets a 15 MHz bandwidth (Jensen class), with 93 µH leakage and 0.46 pF winding capacitance, plus the conductor's own internal inductance. These have nanosecond time constants inside a 10 kΩ loop. At any audio sample rate, trapezoidal integration of such elements produces a slowly decaying ringing at Nyquist (the first build measured it at about −50 dB). Their true in-band effect is at most 3.2e-5 dB, plus an 18.5 ns delay, so they are evaluated analytically, and `verifySuperalloy` proves on every run that leaving them out changes nothing audible.

## Verification (this build, Linux, same sources)

* **verifySuperalloy:** ALL TESTS PASSED (93 checks: sections A–K Subtle, section L Color / Curie / Geiger, section M Bypass). Report: `reports/verifySuperalloy_report.txt`.
* **hostcheckSuperalloy:** ALL CHECKS PASSED (34 checks; the built VST3 loaded in a VST3 host at 48 kHz float, 352.8 kHz float and 352.8 kHz double). Report: `reports/hostcheckSuperalloy_report.txt`.
* **Regression:** Subtle and Color are bit-identical to rev 6.3.1. 0 of 4.76 million samples differ (metal switches, bypass, every settled sample, 44.1 and 352.8 kHz). Inside the 40 ms Subtle ↔ Color crossfade only, the new four-way blend rounds differently by at most 5.6e-17.
* **pluginval:** strictness 10 at 44.1 / 48 / 96 / 192 / 352.8 kHz, block sizes 1 / 64 / 512 / 4096. Result: SUCCESS.
* **Steinberg VST3 validator:** 47 passed, 0 failed.
* **macOS:** GitHub Actions builds the universal VST3 and runs verifySuperalloy, hostcheckSuperalloy and pluginval on Apple Silicon and on Intel hardware.

| Check | Result | Limit |
|---|---|---|
| Subtle: level at 1 kHz, every metal | 0.000000 dB | 0.001 dB |
| Subtle: programme level vs copper (exact pink noise, 20 Hz–20 kHz) | worst 0.0073 dB (René 41) | 0.01 dB (R1) |
| Subtle: shape vs copper, 20 Hz–20 kHz | worst −0.065 dB at 20 Hz | ±0.1 dB (R5) |
| Subtle: superconductor vs DC-free input | −202 dB residual | −120 dB |
| Subtle: engine vs exact Bessel / circuit solution | 7e-6 dB | 1e-4 dB |
| Subtle: switch Copper → René 41 vs a continuously running René 41 | −97 dB (first 250 ms), −140 dB after | −60 / −90 dB |
| Color: response vs flat, every metal, 20 Hz–20 kHz | worst 0.861 dB | ±1 dB |
| Color: difference between any two metals, 20 Hz–20 kHz | worst 0.967 dB (20 Hz) | 1 dB |
| Color: programme level vs Subtle copper (exact pink spectrum) | worst 0.0003 dB | 0.01 dB |
| Color: realised contour vs design | 0.0004 dB | 0.002 dB |
| Color: engine-measured vs analytic response (−40 dBFS) | 0.0006 dB | 0.005 dB |
| Color: Subtle → Color switch vs a continuously running Color channel | exact (−3000 dB) | −120 dB |
| Color: metal switch in Color | −73 dB (first 250 ms), −128 dB after | −60 / −90 dB |
| Curie: response vs flat / any two metals | 2.615 dB / 2.901 dB (2.999× Color) | ±3 dB / 3 dB |
| Geiger: response vs flat / any two metals | 10.653 dB / 11.611 dB (12.001× Color) | ±12 dB / 12 dB |
| Curie / Geiger: realised contour vs exact network | 0.00004 dB / 0.00016 dB | 0.002 dB |
| Curie / Geiger: realised contour vs design | 0.0003 dB / 0.0008 dB | 0.002 dB |
| Curie / Geiger: engine-measured vs analytic response (−40 dBFS) | 0.0006 dB / 0.0006 dB | 0.005 dB |
| Curie / Geiger: programme level vs Subtle copper (exact pink spectrum) | 0.0003 dB / 0.0001 dB | 0.01 dB |
| Curie / Geiger: harmonics follow the contour (no added distortion) | within 0.016 dB / 0.024 dB | 0.05 dB |
| Any position → any position (7 transitions incl. a change mid-fade) vs a channel on the target all along | exact (−3000 dB) | −120 dB |
| Metal switch in Geiger | −77 dB (first 250 ms), −176 dB after | −60 / −90 dB |
| Bypass: settled output vs input (incl. metal + contour changes while bypassed) | exactly 0 | 0 |
| Bypass: re-engaged output vs a channel never bypassed | exactly 0 | 0 |
| Bypass: largest sample step in the 40 ms fades | 0.0425 (programme: 0.0488) | ≤ programme |
| Host: hosted VST3, bypass On vs input (48 kHz float, 352.8 kHz float + double) | exactly 0 | 0 |
| Host: Metal and Contour reach the DSP (René 41 / Geiger residual vs input) | −3.1 dB (48 kHz), −2.7 dB (352.8 kHz) | > −30 dB |
| Host: all four Contour positions reach the DSP, each stronger than the last (René 41, 352.8 kHz) | −21.6 / −11.6 / −7.0 / −2.6 dB | rising |
| Host: state recall of Metal, Contour, Bypass | René 41 / Geiger / On | exact |
| HF purity at 44.1 kHz, 19 kHz 0 dBFS | 6e-7 % THD+N | 1e-4 % |
| CPU | about 1.7× rev 6.3.1 per sample (45 contours instead of 15; same machine, back to back: 183 → 333 ns/sample/channel) | — |

### What each metal does in Subtle (352.8 kHz, −20 dBFS, dB re copper)

| Metal | 20 Hz | 50 Hz | 100 Hz | 20 kHz |
|---|---|---|---|---|
| Superconductor | 0.0000 | 0.0000 | 0.0000 | +0.0010 |
| Silver / Gold / Rhodium / Iridium | ≤ −0.0001 | 0.0000 | 0.0000 | −0.0001 … +0.0007 |
| Pt-Ir 10 | −0.0032 | −0.0005 | −0.0001 | +0.0009 |
| Monel 400 | −0.0133 | −0.0022 | −0.0006 | +0.0010 |
| Haynes 188 / MP35N | −0.043 / −0.044 | −0.007 | −0.002 | +0.0010 |
| Waspaloy / Inconel 718 / Hastelloy C-276 | −0.059 … −0.063 | −0.010 | −0.003 | +0.0010 |
| Inconel 625 / René 41 | −0.065 | −0.011 | −0.003 | +0.0010 |

THD at 20 Hz, 0 dBFS: copper 0.011% · Monel 0.32% · Inconel 718 0.67% · René 41 0.69%.
At 100 Hz, 0 dBFS: copper 0.0008% · Inconel 718 0.042%. At 1 kHz every metal is below 0.0005%.

## Honest notes

1. **Superalloy distortion on deep bass is real physics, not tuning.** A resistive winding is a high source impedance for the core (Whitlock). At 20–50 Hz near full scale it reaches 0.15–0.8%. No cap is applied; this still awaits an owner ruling.
2. **Color rolls off ultrasonically.** The foil winding's eddy currents keep acting above 20 kHz: at 100 kHz Color is −1.0 dB (copper) to −4.5 dB (Monel), and down to −6.1 dB near 158 kHz at 352.8 kHz. This is outside the ±1 dB cap (20 Hz–20 kHz) and does not exist at 44.1/48 kHz.
3. **Color's bass phase lead is larger.** Superalloys: +31° at 20 Hz, +54° at 10 Hz. That is comparable to a gentle 9 Hz subsonic filter. In a null test against the superconductor, Color superalloys null to about −15 dB on pink noise, almost entirely from that phase.
4. **The pink-noise generator was corrected in rev 6.2.** Earlier harnesses used Paul Kellet's IIR pink filter, which is only pink at 44.1 kHz (at 352.8 kHz it is flat below ~75 Hz). With exact pink noise, the Subtle programme-level worst case is 0.0073 dB (previously reported 0.0040 dB). It is still inside the 0.01 dB ruling.
5. **The 15 entries still behave as about seven groups.** Inconel 625 and René 41 are electrically identical, and silver/copper/gold differ only slightly, in every position.
6. **CMSX-4 is not on the menu.** No sourced room-temperature resistivity was found.
7. **Monel 400 is modelled non-magnetic** (owner decision).
8. **How loud the effect is when you hit Bypass depends on the setting, by design.** Measured as the difference between processed and dry on pink noise: Subtle copper −60 dB, Subtle René 41 −26 dB, Color copper −32 dB, Color René 41 −15 dB, Curie copper −24 dB, Curie René 41 −10 dB, Geiger copper −13 dB, Geiger René 41 −3.5 dB. Subtle copper sits below most listeners' threshold in a bypass A/B; Color and the superalloys are clearly audible. The superconductor (−75 dB) is the 0 Ω reference and is meant to be nearly transparent in every position.
9. **Geiger changes each metal's character, not only its depth. That is what the physics does.** In Color the superalloys have the most open top (−0.16 dB at 20 kHz). At Geiger they are −8 dB at 20 kHz, close to iridium (−7.2 dB), and Pt-Ir is the darkest (−10.2 dB). The reason: a foil winding's internal inductance grows as m² and is the same for every metal. At 2 layers it sits in quadrature with the superalloys' large resistance and barely shows; at 7.5 layers it is larger than the load. Superalloys at Geiger: bass −10.6 dB at 20 Hz, mids +1.8 dB (level match), top −8 dB. Pure metals at Geiger: bass +0.9 dB, top −4 dB.
10. **Level-match gain is up to +2.33 dB at Geiger** (superalloys), so their midrange sits above the input level. Peaks can therefore exceed the input's peak level. The engine's clip point is unchanged (+18 dBFS).
11. **Ultrasonic roll-off grows with the level.** At 352.8 kHz, 100 kHz: Color −1 to −4.5 dB, Curie −2.5 to −10 dB, Geiger −8 to −21 dB. This is above the 20 Hz–20 kHz caps, and it does not exist at 44.1/48 kHz.
12. **Bass phase lead grows with the level, in every position and for every metal.** It is the minimum-phase companion of the bass change, not a separate setting. Total phase, René 41, −18 dBFS, measured through the engine:

    | | 20 Hz | 50 Hz | 100 Hz | Bass delay at 50 Hz | at 100 Hz | 20 kHz |
    |---|---|---|---|---|---|---|
    | Subtle | +8° | +3° | +2° | 0.19 ms | 0.05 ms | 0° |
    | Color | +31° | +13° | +7° | 0.73 ms | 0.19 ms | −8° |
    | Curie | +54° | +25° | +13° | 1.31 ms | 0.36 ms | −25° |
    | Geiger | +108° | +68° | +41° | 2.38 ms | 0.98 ms | −62° |

    Copper: +0.2° / +0.5° / +1.0° / +3.3° at 20 Hz; its change is in the top (20 kHz: 0° / −2° / −6° / −19°). The superconductor is unchanged in every position. The left and right channels always get identical phase, so the stereo image and mono fold-down are unaffected. The tables above list the phase the contour adds on top of Subtle.
13. **The programme-level measurement was corrected in rev 6.4.** Earlier harnesses measured on one random pink-noise realisation, whose lowest octaves carry a few percent more or less energy than true pink. That made the measurement itself scatter in proportion to the contour's bass depth: across six seeds, Geiger René 41 read between −0.053 and +0.042 dB, and Color René 41 ±0.007 dB. The harness now plays pink noise with an exact 1/f spectrum (random phase, exact magnitude), periodically, and measures over a full period. Every level then reads within 0.0003 dB for every seed. Section G (Subtle, R1) is unchanged.
14. **Automation recorded on Contour before rev 6.4 needs re-recording.** Saved sessions recall correctly, because the state stores the position number, so Color stays Color. A host automation lane stores 0–1 instead, and 1.0 used to mean Color but now means Geiger.
15. **First load at a new sample rate takes longer.** The 45 contour designs are computed once per sample rate and shared by every instance. They are spread over all CPU cores.

## Files

| File | Purpose |
|---|---|
| `Source/MetalTable.h` | Menu and resistivities with sources |
| `Source/TransformerDesign.h` | Every constant, with its label and origin |
| `Source/TransformerEngine.*` | Nonlinear two-core simulation (2×2 Newton, trapezoidal) |
| `Source/SkinShelf.h` | Exact-skin-effect shelf fit |
| `Source/ColorContour.h` | Contour levels (Color, Curie, Geiger): core factor, Dowell foil winding, contour fits, the 45-contour stage |
| `Source/Channel630.h` | DC block, shadow cores, 40 ms seamless switches, Contour, true bypass |
| `Source/Plugin*.{h,cpp}`, `Source/InconelTexture.h` | JUCE wrapper, Metal dropdown, Contour and Bypass switches, procedural GUI |
| `tools/verifySuperalloy.cpp` | Verification harness (`--calibrate` re-derives u_r) |
| `tools/measureSuperalloy.cpp`, `tools/analyzeSuperalloy.cpp`, `tools/plotAnalysis.py` | Analyser suite and graphs (`reports/analysis`, `reports/analysis_color`) |
| `tools/design.py`, `tools/skinfit.py` | Parasitic, L_m and skin-ladder solvers |
| `tools/designLevels.cpp` | Derivation of the Curie and Geiger geometries from Color (`-DWAVRELY_BUILD_DESIGN=ON`) |
| `tools/hostcheckSuperalloy.cpp` | End-to-end test of the built VST3 in a VST3 host (`-DWAVRELY_BUILD_HOSTCHECK=ON`) |
| `tools/snapshotSuperalloy.cpp` | Off-screen GUI render (`-DWAVRELY_BUILD_SNAPSHOT=ON`) |
| `.github/workflows/macos.yml` | macOS universal build and tests |

## Sources

Lundahl LL1517 datasheet · Jensen JT-11-BMCF and JT-11SS-DLCF datasheets · Whitlock, *Audio Transformers* (Jensen-hosted PDF of Handbook for Sound Engineers ch. 11) · Dowell, "Effects of eddy currents in transformer windings", Proc. IEE 113 (1966) · Heraeus precious-metal property table · Zapp MP35N datasheet · Fort Wayne Metals Haynes 188 datasheet · Special Metals Inconel 625 bulletin · rev 1–5 HANDOFF §3 for the remaining resistivities.
