# Superalloy — rev 6.2

One metal at a time, wound into a step-up → 630 V → step-down transformer pair.
Medium heat-tinted Inconel rectangle with one dropdown, **Metal**, and one two-position switch, **Contour** (Subtle / Color). Zero latency.

* **Subtle** is the rev 6.1 sound, bit for bit (0 of 2.38 million samples differ against the rev 6.1 build, including metal switches, at 44.1 and 352.8 kHz).
* **Color** is the same transformer with an amplified geometry: a core with 1/3.9 of the magnetising inductance and a 2-layer, 2.4 mm foil winding. The metal still enters only through its resistivity. Every metal stays within ±1 dB of flat, and within 1 dB of every other metal, from 20 Hz to 20 kHz. Programme level is matched to within 0.01 dB on pink noise. No distortion mechanism is added.

(Formerly "wavRely 630". Plugin code `Salo`, manufacturer `Wvry`.)

## Build (Windows 11, Visual Studio 2026)

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DWAVRELY_JUCE_DIR=C:/Dev/JUCE
cmake --build build --config Release --target Superalloy_VST3 verifySuperalloy
build\Release\verifySuperalloy.exe
```

* The plugin is written to `build\Superalloy_artefacts\Release\VST3\Superalloy.vst3`. Copy it to `C:\Program Files\Common Files\VST3\`.
* The generator `Visual Studio 18 2026` needs CMake 4.2 or newer.
* `-DWAVRELY_JUCE_DIR` points at your local JUCE 9.0.6 checkout. Leave it out and JUCE 9.0.6 is fetched automatically.
* AAX: add `-DWAVRELY_AAX_SDK=C:/SDKs/aax-sdk`. You then need PACE signing (wraptool) before Pro Tools will load it.
* macOS: built and tested on GitHub Actions (`.github/workflows/macos.yml`): one universal VST3 (Intel + Apple Silicon, macOS 11+), tested on both architectures.
* `verifySuperalloy` must print `RESULT: ALL TESTS PASSED`. Run it after every change to `Source/`.
* Optional tools: `-DWAVRELY_BUILD_MEASURE=ON` builds `measureSuperalloy` (frequency response) and `analyzeSuperalloy` (THD, IMD, level, step, programme null); both take `--color`. `tools/plotAnalysis.py` draws the graphs.

## What is in the signal path

```
DC-free drive (0.01 Hz) → Rp ─┬─ Rs+Rp ─┬─ Rs ─ RL(10 kΩ) → skin shelf → static makeup → CONTOUR
                             Lm1        Lm2                                              Subtle: nothing
                       (T1 84% Ni core)  (T2 84% Ni core)                                Color: per-metal linear contour
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

### The Color contour

Color = the Subtle engine (unchanged, nonlinear), followed by a linear contour equal to (Color network) ÷ (Subtle network) for the selected metal:

| Factor | Physics | Realisation |
|---|---|---|
| Low frequencies | Core at 1/3.9 of the magnetising inductance: both magnetising poles move 3.9× higher. 4× was the brief; at 4× René 41 sat 1.009 dB below copper at 20 Hz, so it was trimmed to hold the 1 dB cap between any two metals. | Exact closed-form pole/zero ratio, two first-order sections |
| Mid and high frequencies | 2-layer, 2.4 mm foil winding: the proximity effect (Dowell's multilayer eddy-current solution) raises each winding's AC resistance where that metal's skin depth falls below the foil thickness. It replaces the Subtle round-wire skin effect. | Three first-order minimum-phase sections, fitted in the digital domain at every sample rate (bounded Levenberg–Marquardt, analytic Jacobian), error ≤ 0.0005 dB in band |
| Level | Pink noise band-limited 20 Hz–20 kHz (2nd-order Butterworth), matched to the input level | One gain per metal |

Every metal's contour runs continuously on the engine output, so a metal change or a Subtle ↔ Color change is a 40 ms equal-gain crossfade between signals that are already settled.

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

### Why the MHz parasitics are analytic, not simulated

The composite targets a 15 MHz bandwidth (Jensen class), with 93 µH leakage and 0.46 pF winding capacitance, plus the conductor's own internal inductance. These have nanosecond time constants inside a 10 kΩ loop. At any audio sample rate, trapezoidal integration of such elements produces a slowly decaying ringing at Nyquist (the first build measured it at about −50 dB). Their true in-band effect is at most 3.2e-5 dB, plus an 18.5 ns delay, so they are evaluated analytically, and `verifySuperalloy` proves on every run that leaving them out changes nothing audible.

## Verification (this build, Linux, same sources)

* **verifySuperalloy:** ALL TESTS PASSED (42 checks: sections A–K Subtle, section L Color). Report: `reports/verifySuperalloy_report.txt`.
* **pluginval:** strictness 10 at 44.1 / 48 / 96 / 192 / 352.8 kHz, block sizes 1 / 64 / 512 / 4096. Result: SUCCESS.
* **Steinberg VST3 validator:** 47 passed, 0 failed.
* **macOS:** GitHub Actions builds the universal VST3 and runs verifySuperalloy + pluginval on Apple Silicon and on Intel hardware.

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
| Color: programme level vs Subtle copper (exact pink noise) | worst 0.0032 dB | 0.01 dB |
| Color: realised contour vs design | 0.0004 dB | 0.002 dB |
| Color: engine-measured vs analytic response (−40 dBFS) | 0.0006 dB | 0.005 dB |
| Color: Subtle → Color switch vs a continuously running Color channel | exact (−3000 dB) | −120 dB |
| Color: metal switch in Color | −73 dB (first 250 ms), −128 dB after | −60 / −90 dB |
| HF purity at 44.1 kHz, 19 kHz 0 dBFS | 6e-7 % THD+N | 1e-4 % |
| CPU | rev 6.1 + 15–25 % (same machine, same run) | — |

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
5. **The 15 entries still behave as about seven groups.** Inconel 625 and René 41 are electrically identical, and silver/copper/gold differ only slightly, in both positions.
6. **CMSX-4 is not on the menu.** No sourced room-temperature resistivity was found.
7. **Monel 400 is modelled non-magnetic** (owner decision).

## Files

| File | Purpose |
|---|---|
| `Source/MetalTable.h` | Menu and resistivities with sources |
| `Source/TransformerDesign.h` | Every constant, with its label and origin |
| `Source/TransformerEngine.*` | Nonlinear two-core simulation (2×2 Newton, trapezoidal) |
| `Source/SkinShelf.h` | Exact-skin-effect shelf fit |
| `Source/ColorContour.h` | Color physics (core ÷3.9, Dowell foil winding), contour fit, Color stage |
| `Source/Channel630.h` | DC block, shadow cores, 40 ms seamless switches, Contour |
| `Source/Plugin*.{h,cpp}`, `Source/InconelTexture.h` | JUCE wrapper, Metal dropdown, Contour switch, procedural GUI |
| `tools/verifySuperalloy.cpp` | Verification harness (`--calibrate` re-derives u_r) |
| `tools/measureSuperalloy.cpp`, `tools/analyzeSuperalloy.cpp`, `tools/plotAnalysis.py` | Analyser suite and graphs (`reports/analysis`, `reports/analysis_color`) |
| `tools/design.py`, `tools/skinfit.py` | Parasitic, L_m and skin-ladder solvers |
| `tools/snapshotSuperalloy.cpp` | Off-screen GUI render (`-DWAVRELY_BUILD_SNAPSHOT=ON`) |
| `.github/workflows/macos.yml` | macOS universal build and tests |

## Sources

Lundahl LL1517 datasheet · Jensen JT-11-BMCF and JT-11SS-DLCF datasheets · Whitlock, *Audio Transformers* (Jensen-hosted PDF of Handbook for Sound Engineers ch. 11) · Dowell, "Effects of eddy currents in transformer windings", Proc. IEE 113 (1966) · Heraeus precious-metal property table · Zapp MP35N datasheet · Fort Wayne Metals Haynes 188 datasheet · Special Metals Inconel 625 bulletin · rev 1–5 HANDOFF §3 for the remaining resistivities.
