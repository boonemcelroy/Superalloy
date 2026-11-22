/*
  ==============================================================================
    Superalloy — TransformerDesign.h
    The Superalloy composite reference transformer ("best of the best").
    One consistent virtual geometry; every metal is wound on it identically.

    Labels: [D] published value  [P] derived by standard equation / solver
            [A] assumption        [INV] owner-approved invention

    Full composite circuit (all values referred to line level, 1:1; the 630 V
    bus is the same circuit scaled by the virtual turns ratio N — linear
    transfer is voltage-invariant, so the scaling cancels exactly):

      v_in ─ Rp(skin) ─┬─ Ll ─ Rs(skin) ─┬─ Rp(skin) ─┬─ Ll ─ Rs(skin) ─┬─ out
                      Lm(core)          C          Lm(core)          C ‖ RL
           └──────── T1 step-up ─────────┘└──────── T2 step-down ───────┘

    Real-time realisation (see README, "Why the MHz parasitics are analytic"):
      • nonlinear engine: Rp, Lm(core), Rs+Rp, Lm(core), Rs, RL  (TransformerEngine)
      • skin effect (resistive part): exact Bessel solution realised as a fitted
        minimum-phase shelf, error ≤ 2e-5 dB                       (SkinShelf)
      • leakage Ll, winding C, conductor internal inductance: computed here
        analytically; their in-band effect is < 1e-4 dB (verified by the harness),
        but at audio sample rates they cannot be discretised without Nyquist
        ringing, so they are not simulated in real time.
  ==============================================================================
*/
#pragma once

#include "MetalTable.h"
#include <cmath>
#include <complex>
#include <array>

namespace superalloy::design
{
    inline constexpr double kPi  = 3.14159265358979323846;
    inline constexpr double kMu0 = 4.0e-7 * kPi;
    using cd = std::complex<double>;

    //-------------------------------------------------------------- level map
    // [A] 0 dBFS full-scale sine = +24 dBu RMS (SMPTE RP155 alignment).
    inline constexpr double kVoltsAtFullScale = 17.37147; // peak volts for sample value 1.0

    //------------------------------------------------------------ copper ref
    // [D] Lundahl LL1517 line output: 9.2 Ω per primary, 9.5 Ω per secondary.
    inline constexpr double kRhoCopper = 16.78e-9;
    inline constexpr double kRpCopper  = 9.2;
    inline constexpr double kRsCopper  = 9.5;

    // [A] Load: modern bridging mastering input.
    inline constexpr double kRLoad = 10000.0;

    //------------------------------------------------------------ parasitics
    // [P] Leakage and winding capacitance: Butterworth-damped into RL per
    // transformer, scaled so the copper cascade first reaches −3 dB at 15 MHz
    // (target from Jensen JT-11-BMCF / JT-11SS-DLCF [D]). Values sit between
    // OEP 1:1 (300 µH) / LL1517 (0.3 mH) leakage [D] and Lundahl's amorphous
    // "<1 pF" winding capacitance [D]. Solver: tools/design.py. Analytic only.
    inline constexpr double kLeakage = 9.296e-5;   // H
    inline constexpr double kWindC   = 4.648e-13;  // F

    //------------------------------------------------------------------ core
    // [P] Initial magnetising inductance: copper cascade −3 dB at 0.04 Hz
    // (target from Jensen JT-11SS-DLCF [D]). Solver: tools/design.py.
    inline constexpr double kLmInitial = 129.5; // H

    // 84 % nickel core magnetisation, i = g(φ):
    //   L_i·g'(φ) = r + (1−r)·exp(−|u|/u_r) + r·|u|^p ,  u = φ/φ_s
    // Low flux: Rayleigh-type permeability rise (distortion ∝ level / f²,
    // matching Whitlock's "roughly quarters per doubling of frequency" [D]).
    // Plateau at µmax, then saturation with the knee at u = 1.
    inline constexpr double kCoreR = 0.25; // [A] µi/µmax for 80–84 % Ni alloys
    inline constexpr double kCoreP = 6.0;  // [A] saturation sharpness
    // [P] u_r calibrated (tools/verifySuperalloy --calibrate, 352.8 kHz) so the COPPER
    // cascade gives 0.002 % THD at 20 Hz, +4 dBu (−20 dBFS): Jensen JT-11-BMCF [D].
    inline constexpr double kCoreUrCalibrated = 0.08578808142;

    // Owner ruling R6: core knee at +18 dBFS for a 20 Hz sine (flux ∝ V/f, so
    // every higher frequency has more margin). [P]
    inline constexpr double kClipPointDBFS = 18.0;
    inline constexpr double kClipRefHz     = 20.0;
    inline double fluxKnee() noexcept
    {
        return kVoltsAtFullScale * std::pow (10.0, kClipPointDBFS / 20.0) / (2.0 * kPi * kClipRefHz);
    }
    // Narrative: 630 V on the bus exactly at the core knee → N = 630 / V_knee.
    inline double virtualTurnsRatio() noexcept
    {
        return 630.0 / (kVoltsAtFullScale * std::pow (10.0, kClipPointDBFS / 20.0));
    }

    //------------------------------------------------------------ skin effect
    // [INV: slight] Owner-approved slight exaggeration: the virtual winding
    // conductor is AWG 18 (1.024 mm) so the skin knee of the pure metals lands
    // near the top of the audio band. Knee f_k = rho / (π µ0 r²).
    inline constexpr double kWireRadius = 1.024e-3 * 0.5;

    // Superconductor stand-in (keeps the network well-posed).
    inline constexpr double kSuperconductorOhms = 1.0e-6;

    //------------------------------------------------------- input coupling
    // [A] Driving stage is DC-free (Jensen datasheets require DC-free drive [D]):
    // 1st-order DC blocker at 0.01 Hz, identical for every metal.
    inline constexpr double kDcBlockHz = 0.01;

    //====================================================================
    struct MetalWinding
    {
        double Rp, Rs;   // Ω, per winding
        double fk;       // skin knee, Hz (0 = no skin effect)
        double Rtotal() const noexcept { return 2.0 * (Rp + Rs); }
    };

    inline MetalWinding windingFor (int metalIndex) noexcept
    {
        const double rho = kMetals[(size_t) metalIndex].rho;
        if (rho <= 0.0)
            return { kSuperconductorOhms, kSuperconductorOhms, 0.0 };
        const double k = rho / kRhoCopper;
        return { kRpCopper * k, kRsCopper * k, rho / (kPi * kMu0 * kWireRadius * kWireRadius) };
    }

    //====================================================================
    // Exact round-wire internal impedance, Z/Rdc = (z/2)·J0(z)/J1(z),
    // z = (r/δ)(1 − j), r/δ = sqrt(f/f_k). Power series (|z| < 12 here).
    inline cd skinZn (double hz, double fk) noexcept
    {
        if (fk <= 0.0 || hz <= 0.0) return cd (1.0, 0.0);
        const double x = std::sqrt (hz / fk);
        if (x > 8.0) return cd (x * 0.5 + 0.25, x * 0.5);       // asymptote
        const cd z = cd (x, -x);
        const cd q = -(z * z) * 0.25;
        cd term0 = 1.0, sum0 = 1.0;          // J0
        cd term1 = z * 0.5, sum1 = term1;    // J1
        for (int k = 1; k < 80; ++k)
        {
            term0 *= q / (double) (k * k);
            term1 *= q / (double) (k * (k + 1));
            sum0 += term0; sum1 += term1;
        }
        return (z * 0.5) * sum0 / sum1;
    }

    //====================================================================
    // Real-time network: Rp ─ Lm ─ (Rs+Rp) ─ Lm ─ Rs ─ RL, core at L_i.
    inline cd networkTransfer (int metalIndex, double hz, double lm = kLmInitial) noexcept
    {
        const auto w = windingFor (metalIndex);
        const cd s (0.0, 2.0 * kPi * hz);
        const cd Ym = 1.0 / (s * lm);
        const double R1 = w.Rp, R2 = w.Rs + w.Rp, R3 = w.Rs;
        const cd Za2 = 1.0 / (Ym + 1.0 / (R3 + kRLoad));
        const cd Za1 = 1.0 / (Ym + 1.0 / (R2 + Za2));
        return (Za1 / (R1 + Za1)) * (Za2 / (R2 + Za2)) * (kRLoad / (R3 + kRLoad));
    }

    // Exact skin-effect ratio (magnitude) relative to the DC-resistance network.
    // At frequencies where skin matters the cores are open, so the windings are
    // a series string of total resistance Rtot·Zn(f) feeding RL.
    inline double skinRatioExact (int metalIndex, double hz) noexcept
    {
        const auto w = windingFor (metalIndex);
        if (w.fk <= 0.0) return 1.0;
        const double Rt = w.Rtotal();
        return std::abs ((kRLoad + Rt) / (kRLoad + Rt * skinZn (hz, w.fk)));
    }

    // Full composite (skin incl. internal inductance, leakage, winding C):
    // used ONLY to prove the omitted parasitics are inaudible.
    inline cd fullCompositeTransfer (int metalIndex, double hz, bool withParasitics = true) noexcept
    {
        const auto w = windingFor (metalIndex);
        const cd s (0.0, 2.0 * kPi * hz);
        const cd zn = skinZn (hz, w.fk);
        const cd Zp = w.Rp * zn, Zs = w.Rs * zn;
        const cd ZL = withParasitics ? s * kLeakage : cd (0.0);
        const cd Yc = withParasitics ? s * kWindC   : cd (0.0);
        const cd Ym = 1.0 / (s * kLmInitial);
        const cd Zb2 = 1.0 / (Yc + 1.0 / kRLoad);
        const cd Za2 = 1.0 / (Ym + 1.0 / (ZL + Zs + Zb2));
        const cd Zb1 = 1.0 / (Yc + 1.0 / (Zp + Za2));
        const cd Za1 = 1.0 / (Ym + 1.0 / (ZL + Zs + Zb1));
        const cd vA1 = Za1 / (Zp + Za1);
        const cd vB1 = vA1 * Zb1 / (ZL + Zs + Zb1);
        const cd vA2 = vB1 * Za2 / (Zp + Za2);
        return vA2 * Zb2 / (ZL + Zs + Zb2);
    }

    inline double warpedHz (double hz, double fs) noexcept
    {
        return fs / kPi * std::tan (kPi * hz / fs);
    }
}
