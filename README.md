# Superalloy — rev 6.1 (formerly "wavRely 630")

Renamed from wavRely 630: product, CMake targets (`Superalloy`, `verifySuperalloy`), classes, namespace and plugin code (`Salo`). The DSP is unchanged; the checks were re-run after the rename.

One metal at a time, wound into a step-up → 630 V → step-down transformer pair.
Medium heat-tinted Inconel rectangle, one dropdown: **Metal**. Zero latency.

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
* macOS: `cmake -S . -B build -G Xcode`. This builds a universal binary; you then sign and notarise it.
* `verifySuperalloy` must print `RESULT: ALL TESTS PASSED`. Run it after every change to `Source/`.

## What is in the signal path

```
DC-free drive (0.01 Hz) → Rp ─┬─ Rs+Rp ─┬─ Rs ─ RL(10 kΩ) → skin shelf → static makeup
                             Lm1        Lm2
                       (T1 84% Ni core)  (T2 84% Ni core)
```

The metal changes only the winding resistance (∝ ρ) and the skin knee (∝ ρ). Nothing else differs between menu entries.

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

### Why the MHz parasitics are analytic, not simulated

The composite targets a 15 MHz bandwidth (Jensen class), with 93 µH leakage and 0.46 pF winding capacitance, plus the conductor's own internal inductance. These have nanosecond time constants inside a 10 kΩ loop.

At any audio sample rate, trapezoidal integration of such elements produces a slowly decaying ringing at Nyquist. The first build measured it at about −50 dB. Integrators that suppress the ringing (backward Euler, BDF2) instead put a −0.01 to −0.03 dB error at 20 kHz when running at 44.1 kHz.

Their true in-band effect is at most 3.2e-5 dB, plus an 18.5 ns delay, so they are evaluated analytically. `verifySuperalloy` proves on every run that leaving them out changes nothing audible. The *resistive* part of the skin effect does matter for the metals, so it is realised as a fitted minimum-phase shelf whose error is ≤ 2e-5 dB at every rate.

## Verification (this build, Linux, same sources)

* **verifySuperalloy:** ALL TESTS PASSED (22 checks). The full report is in `reports/verifySuperalloy_report.txt`.
* **pluginval:** strictness 10 at 44.1 / 48 / 96 / 192 / 352.8 kHz, block sizes 1 / 64 / 512 / 4096. Result: SUCCESS.
* **Steinberg VST3 validator:** 47 passed, 0 failed.

Key numbers:

| Check | Result | Limit |
|---|---|---|
| Level at 1 kHz, every metal | 0.000000 dB | 0.001 dB |
| Programme level vs copper (pink noise, 20 Hz–20 kHz) | worst 0.0040 dB (René 41) | 0.01 dB (R1) |
| Shape vs copper, 20 Hz–20 kHz | worst −0.065 dB at 20 Hz (René 41 / Inconel 625) | ±0.1 dB (R5) |
| Superconductor vs DC-free input | −204 dB residual | −120 dB |
| Engine vs exact Bessel / circuit solution | 7e-6 dB | 1e-4 dB |
| Switch Copper → René 41 vs a continuously running René 41 | −93 dB (first 250 ms), −137 dB after | −60 / −90 dB |
| HF purity at 44.1 kHz, 19 kHz 0 dBFS | 6e-7 % THD+N | 1e-4 % |
| CPU at 352.8 kHz, stereo | ≈12% of one core (×2 for 40 ms during a switch) | — |

### What each metal does (352.8 kHz, −20 dBFS, dB re copper)

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

1. **Superalloy distortion on deep bass is real physics, not tuning.** A resistive winding is a high source impedance for the core, and Whitlock's rule applies: source impedance above the winding's DC resistance raises distortion. At 20–50 Hz near full scale it reaches 0.15–0.7%. That is audible thickening of sub-bass on hot masters. If that counts as "changing the mix", it needs a ruling (see the project brief). No cap is applied in this build.
2. **Silver, copper and gold differ by ≤ 0.0003 dB.** That is what "slightly exaggerated" skin effect yields when the windings are 0.4% of a 10 kΩ circuit.
3. **Inconel 625 and René 41 are electrically identical** within published resistivity, so they measure and sound identical.
4. **CMSX-4 is not on the menu.** No sourced room-temperature resistivity was found.
5. **Monel 400 is modelled non-magnetic** (owner decision).

## Files

| File | Purpose |
|---|---|
| `Source/MetalTable.h` | Menu and resistivities with sources |
| `Source/TransformerDesign.h` | Every constant, with its label and origin |
| `Source/TransformerEngine.*` | Nonlinear two-core simulation (2×2 Newton, trapezoidal) |
| `Source/SkinShelf.h` | Exact-skin-effect shelf fit |
| `Source/Channel630.h` | DC block, shadow cores, 40 ms seamless switch |
| `Source/Plugin*.{h,cpp}`, `Source/InconelTexture.h` | JUCE wrapper and procedural GUI |
| `tools/verifySuperalloy.cpp` | Verification harness (`--calibrate` re-derives u_r) |
| `tools/design.py`, `tools/skinfit.py` | Parasitic, L_m and skin-ladder solvers |
| `tools/snapshotSuperalloy.cpp` | Off-screen GUI render (`-DWAVRELY_BUILD_SNAPSHOT=ON`) |

## Sources

Lundahl LL1517 datasheet · Jensen JT-11-BMCF and JT-11SS-DLCF datasheets · Whitlock, *Audio Transformers* (Jensen-hosted PDF of Handbook for Sound Engineers ch. 11) · Heraeus precious-metal property table · Zapp MP35N datasheet · Fort Wayne Metals Haynes 188 datasheet · Special Metals Inconel 625 bulletin · rev 1–5 HANDOFF §3 for the remaining resistivities.
