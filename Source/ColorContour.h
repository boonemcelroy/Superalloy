/*
  ==============================================================================
    Superalloy — ColorContour.h
    The CONTOUR switch: Subtle (rev 6.1 sound, bit-identical) / Color.

    Color is the same composite transformer with an amplified geometry. Every
    metal is still wound identically; the metal still enters ONLY through rho.

      1. Core: 1/3.9 of the magnetising inductance  [INV, owner-approved ~4x]
         -> every metal's two low-frequency poles move 3.9x higher. (4x was
            the brief; 4x put René 41 1.009 dB below copper at 20 Hz, so it is
            trimmed to keep ANY two metals within the 1 dB cap, not only each
            metal versus flat.) High-resistivity metals tighten the deep bass
            (René 41: about −0.86 dB at 20 Hz); the pure metals are untouched.
      2. Winding: 2-layer foil, 2.4 mm thick       [INV, owner-approved]
         -> the proximity effect (Dowell's multilayer eddy-current solution)
            raises each winding's AC resistance where the skin depth of that
            metal falls below the foil thickness. Pure metals: a gentle
            smoothing from ~2 kHz upward. Rhodium / iridium: deepest at
            20 kHz. Superalloys: almost none (their skin depth is too large).

    Realisation (why distortion does not change):
      Color = Subtle engine (unchanged, nonlinear) -> per-metal LINEAR contour
      filter = (Color network) / (Subtle network). The contour adds no
      nonlinearity. Its factors:
        • low frequencies: exact pole/zero ratio Π (s+p_i)/(s+k·p_i), k = kCoreFactor, where p_i
          are the Subtle network's two magnetising poles (closed form)
        • high frequencies: Dowell foil response divided by the round-wire
          skin response Subtle already contains; realised as three cascaded
          first-order minimum-phase sections fitted in the DIGITAL domain at
          prepare time (warping included), bounded Levenberg–Marquardt on the
          dB error; in-band error ≤ 0.001 dB (checked by the harness).
      Each metal's Color output is level-matched to the input on pink noise
      band-limited 20 Hz – 20 kHz (owner ruling: ≤ 0.01 dB programme level).

    Switching: every metal's contour filter runs continuously on the engine
    output, so a metal change or a Subtle/Color change is a pure 40 ms
    equal-gain crossfade between signals that are already settled.
  ==============================================================================
*/
#pragma once

#include "TransformerDesign.h"
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <vector>
#include <algorithm>

namespace superalloy
{
    namespace colorDesign
    {
        using design::cd;
        using design::kPi;
        using design::kMu0;

        inline constexpr double kCoreFactor    = 3.9;      // [INV] Lm / 3.9 (owner: 4x, trimmed to hold the 1 dB cap between any two metals)
        inline constexpr double kFoilThickness = 2.4e-3;   // [INV] m
        inline constexpr int    kFoilLayers    = 2;        // [INV]
        inline constexpr double kCapDB         = 1.0;      // owner ruling: ±1 dB, 20 Hz – 20 kHz

        // Dowell: Z_ac / R_dc of an m-layer foil winding of thickness h.
        inline cd dowellZn (double hz, double rho) noexcept
        {
            if (rho <= 0.0 || hz <= 0.0) return cd (1.0, 0.0);
            const double delta = std::sqrt (rho / (kPi * hz * kMu0));
            const cd x = cd (1.0, 1.0) * (kFoilThickness / delta);
            const double m = (double) kFoilLayers;
            return x * (1.0 / std::tanh (x) + (2.0 * (m * m - 1.0) / 3.0) * std::tanh (x * 0.5));
        }

        // Network with complex winding impedances and a given core inductance.
        inline cd network (cd R1, cd R2, cd R3, double lm, double hz) noexcept
        {
            const cd s (0.0, 2.0 * kPi * hz);
            const cd Ym = 1.0 / (s * lm);
            const cd Za2 = 1.0 / (Ym + 1.0 / (R3 + design::kRLoad));
            const cd Za1 = 1.0 / (Ym + 1.0 / (R2 + Za2));
            return (Za1 / (R1 + Za1)) * (Za2 / (R2 + Za2)) * (design::kRLoad / (R3 + design::kRLoad));
        }

        // Exact Color/Subtle ratio of the full networks (harness reference).
        inline cd exactRatio (int metal, double hz) noexcept
        {
            const auto w = design::windingFor (metal);
            const double rho = kMetals[(size_t) metal].rho;
            const cd zf = dowellZn (hz, rho), zs = design::skinZn (hz, w.fk);
            const cd color  = network (w.Rp * zf, (w.Rs + w.Rp) * zf, w.Rs * zf, design::kLmInitial / kCoreFactor, hz);
            const cd subtle = network (w.Rp * zs, (w.Rs + w.Rp) * zs, w.Rs * zs, design::kLmInitial, hz);
            return color / subtle;
        }

        // High-frequency factor: cores open, series winding string into RL.
        inline double hfTargetDB (int metal, double hz) noexcept
        {
            const auto w = design::windingFor (metal);
            const double Rt = w.Rtotal();
            const cd zf = dowellZn (hz, kMetals[(size_t) metal].rho), zs = design::skinZn (hz, w.fk);
            return 20.0 * std::log10 (std::abs ((design::kRLoad + Rt * zs) / (design::kRLoad + Rt * zf)));
        }

        // Subtle network's two magnetising poles (rad/s): p_i = 1 / (Lm · eig_i(G)).
        inline std::array<double, 2> subtlePoles (int metal) noexcept
        {
            const auto w = design::windingFor (metal);
            const double G1 = 1.0 / w.Rp, G2 = 1.0 / (w.Rs + w.Rp), G3 = 1.0 / (w.Rs + design::kRLoad);
            const double a = G1 + G2, b = -G2, c = G2 + G3;
            const double tr = a + c, det = a * c - b * b;
            const double disc = std::sqrt (std::max (0.0, tr * tr * 0.25 - det));
            const double l1 = tr * 0.5 + disc, l2 = tr * 0.5 - disc;
            return { 1.0 / (design::kLmInitial * l1), 1.0 / (design::kLmInitial * l2) };
        }
    }

    //==========================================================================
    // First-order section, transposed direct form II.
    struct FirstOrder
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0;

        // Analog (s + wz)/(s + wp) · g, both corners pre-warped, bilinear.
        static FirstOrder make (double wz, double wp, double g, double fs) noexcept
        {
            const double K = 2.0 * fs;
            const double z = K * std::tan (wz / K), p = K * std::tan (wp / K);
            FirstOrder f;
            f.b0 = g * (K + z) / (K + p);
            f.b1 = g * (z - K) / (K + p);
            f.a1 = (p - K) / (K + p);
            return f;
        }
        // |H|² at normalised angular frequency, from cos(ω)
        double mag2 (double cw) const noexcept
        {
            return (b0 * b0 + b1 * b1 + 2.0 * b0 * b1 * cw) / (1.0 + a1 * a1 + 2.0 * a1 * cw);
        }
    };

    //==========================================================================
    struct ColorDesign
    {
        static constexpr int kLF = 2, kHF = 3, kSections = kLF + kHF;
        std::array<FirstOrder, kSections> sec {};
        double gain = 1.0;          // pink-noise level match
        double fitErrorDB = 0.0;    // in-band minimax error of the HF fit

        double magnitudeAt (double hz, double fs) const noexcept   // without gain
        {
            const double cw = std::cos (2.0 * design::kPi * hz / fs);
            double m2 = 1.0;
            for (const auto& s : sec) m2 *= s.mag2 (cw);
            return std::sqrt (m2);
        }

        // subtleMag(hz): magnitude of the Subtle chain (engine + skin shelf + makeup)
        void design (int metal, double fs, const std::function<double (double)>& subtleMag)
        {
            using namespace colorDesign;
            sec.fill (FirstOrder {});
            fitErrorDB = 0.0;

            // 1. low frequencies: exact
            const auto p = subtlePoles (metal);
            for (int i = 0; i < kLF; ++i)
                sec[(size_t) i] = FirstOrder::make (p[(size_t) i], kCoreFactor * p[(size_t) i], 1.0, fs);

            // 2. high frequencies: minimax fit of three first-order cut sections
            fitHF (metal, fs);

            // 3. level match on pink noise band-limited 20 Hz – 20 kHz by 2nd-order
            //    Butterworth filters (the programme-level definition the harness
            //    measures, ruling R1): pink = uniform weight per log-frequency.
            constexpr int kN = 4000;
            const double fLo = 2.0, fHi = std::min (100000.0, 0.49 * fs);
            const double tHp = std::tan (kPi * 20.0 / fs), tLp = std::tan (kPi * 20000.0 / fs);
            double acc = 0.0, ref = 0.0;
            for (int i = 0; i < kN; ++i)
            {
                const double hz = fLo * std::pow (fHi / fLo, (i + 0.5) / kN);
                const double t = std::tan (kPi * hz / fs);
                const double hp4 = std::pow (t / tHp, 4.0), lp4 = std::pow (t / tLp, 4.0);
                const double band = (hp4 / (1.0 + hp4)) / (1.0 + lp4);
                const double m = subtleMag (hz) * magnitudeAt (hz, fs);
                acc += band * m * m;
                ref += band;
            }
            gain = 1.0 / std::sqrt (acc / ref);
        }

    private:
        // HF fit. A bilinear first-order section with PRE-WARPED corners z, p has
        // |H|² = (Ω² + z²)·p² / ((Ω² + p²)·z²) exactly, where Ω = 2fs·tan(ω/2) is
        // the warped frequency of the grid point. So the digital fit is done in
        // (ln z, ln p) with an analytic Jacobian: bounded Levenberg–Marquardt on
        // the weighted dB error, three starts, best kept.
        void fitHF (int metal, double fs)
        {
            using namespace colorDesign;
            constexpr int G = kGrid, D = 2 * kHF;
            const double fmax = std::min (0.45 * fs, 100000.0);
            const double K = 2.0 * fs;
            std::array<double, G> W2 {}, tgt {}, wt {}, hzv {};
            for (int i = 0; i < G; ++i)
            {
                hzv[(size_t) i] = 10.0 * std::pow (fmax / 10.0, (double) i / (G - 1));
                const double Om = K * std::tan (kPi * hzv[(size_t) i] / fs);
                W2[(size_t) i] = Om * Om;
                tgt[(size_t) i] = hfTargetDB (metal, hzv[(size_t) i]);
                wt[(size_t) i] = hzv[(size_t) i] <= 20000.0 ? 1.0 : 0.1;
            }
            if (std::abs (tgt[(size_t) G - 1]) < 1.0e-9) return;     // superconductor: identity

            constexpr double c10 = 10.0 / 2.302585092994046;
            const double lo = std::log (2.0 * kPi * 2.0), hi = std::log (K * 50.0);

            // q = { ln p0, ln z0, ln p1, ln z1, ... } (warped rad/s)
            auto evalAll = [&] (const double* q, double* r, double (*J)[D]) noexcept
            {
                for (int i = 0; i < G; ++i)
                {
                    double y = 0.0;
                    for (int k = 0; k < kHF; ++k)
                    {
                        const double p2 = std::exp (2.0 * q[2 * k]), z2 = std::exp (2.0 * q[2 * k + 1]);
                        const double ap = W2[(size_t) i] + p2, az = W2[(size_t) i] + z2;
                        y += c10 * std::log ((az * p2) / (ap * z2));
                        if (J != nullptr)
                        {
                            J[i][2 * k]     = wt[(size_t) i] * c10 * (2.0 - 2.0 * p2 / ap);
                            J[i][2 * k + 1] = wt[(size_t) i] * c10 * (2.0 * z2 / az - 2.0);
                        }
                    }
                    r[i] = wt[(size_t) i] * (y - tgt[(size_t) i]);
                }
            };

            double best[D] {}, bestCost = 1.0e300;
            for (double base : { 700.0, 2000.0, 5000.0 })
            {
                double q[D];
                for (int k = 0; k < kHF; ++k)
                {
                    const double fp = base * std::pow (4.0, k);
                    q[2 * k]     = std::clamp (std::log (2.0 * kPi * fp), lo, hi);
                    q[2 * k + 1] = std::clamp (std::log (2.0 * kPi * fp * 1.5), lo, hi);
                }
                const double c = levenbergMarquardt (evalAll, q, lo, hi);
                if (c < bestCost) { bestCost = c; std::copy (q, q + D, best); }
            }

            double r[G];
            evalAll (best, r, nullptr);
            double inband = 0.0;
            for (int i = 0; i < G; ++i)
                if (hzv[(size_t) i] <= 20000.0) inband = std::max (inband, std::abs (r[i] / wt[(size_t) i]));
            fitErrorDB = inband;

            for (int k = 0; k < kHF; ++k)
            {
                const double p = std::exp (best[2 * k]), z = std::exp (best[2 * k + 1]);
                FirstOrder f;
                const double g = p / z;                       // unity at DC
                f.b0 = g * (K + z) / (K + p);
                f.b1 = g * (z - K) / (K + p);
                f.a1 = (p - K) / (K + p);
                sec[(size_t) (kLF + k)] = f;
            }
        }

        static constexpr int kGrid = 140;

        template <typename F>
        static double levenbergMarquardt (F& evalAll, double* q, double lo, double hi) noexcept
        {
            constexpr int D = 2 * kHF, G = kGrid;
            double r[G], rt[G], J[G][D];
            auto sumsq = [] (const double* v) { double s = 0; for (int i = 0; i < G; ++i) s += v[i] * v[i]; return s; };
            evalAll (q, r, J);
            double cost = sumsq (r), lambda = 1.0e-3;
            for (int iter = 0; iter < 250; ++iter)
            {
                double A[D][D] = {}, g[D] = {};
                for (int i = 0; i < G; ++i)
                    for (int a = 0; a < D; ++a)
                    {
                        g[a] += J[i][a] * r[i];
                        for (int b = 0; b < D; ++b) A[a][b] += J[i][a] * J[i][b];
                    }
                bool improved = false;
                for (int tries = 0; tries < 16 && ! improved; ++tries)
                {
                    double M[D][D + 1];
                    for (int a = 0; a < D; ++a)
                    {
                        for (int b = 0; b < D; ++b) M[a][b] = A[a][b] + (a == b ? lambda * (A[a][a] + 1.0e-9) : 0.0);
                        M[a][D] = -g[a];
                    }
                    for (int c = 0; c < D; ++c)
                    {
                        int p = c;
                        for (int rr = c + 1; rr < D; ++rr) if (std::abs (M[rr][c]) > std::abs (M[p][c])) p = rr;
                        for (int k = 0; k <= D; ++k) std::swap (M[c][k], M[p][k]);
                        if (std::abs (M[c][c]) < 1.0e-300) continue;
                        for (int rr = 0; rr < D; ++rr)
                        {
                            if (rr == c) continue;
                            const double f = M[rr][c] / M[c][c];
                            for (int k = c; k <= D; ++k) M[rr][k] -= f * M[c][k];
                        }
                    }
                    double qn[D];
                    for (int a = 0; a < D; ++a)
                        qn[a] = std::clamp (q[a] + (std::abs (M[a][a]) >= 1.0e-300 ? M[a][D] / M[a][a] : 0.0), lo, hi);
                    evalAll (qn, rt, nullptr);
                    const double cn = sumsq (rt);
                    if (cn < cost)
                    {
                        const double rel = (cost - cn) / std::max (cost, 1.0e-300);
                        std::copy (qn, qn + D, q);
                        cost = cn;
                        lambda = std::max (lambda * 0.3, 1.0e-15);
                        improved = true;
                        evalAll (q, r, J);
                        if (rel < 1.0e-9) return cost;
                    }
                    else
                        lambda *= 5.0;
                }
                if (! improved) break;
            }
            return cost;
        }
    };

    //==========================================================================
    // Per-channel Color stage. Runs every metal's contour continuously.
    // Structure-of-arrays: each section is processed for all metals at once, so
    // the inner loops over metals vectorise (the metals are independent).
    class ColorStage
    {
    public:
        static constexpr double kFadeSeconds = 0.040;
        static constexpr int kM = kNumMetals, kS = ColorDesign::kSections;

        void prepare (const std::array<ColorDesign, kNumMetals>& d, double fs, bool colorOn) noexcept
        {
            for (int k = 0; k < kS; ++k)
                for (int m = 0; m < kM; ++m)
                {
                    const auto& sc = d[(size_t) m].sec[(size_t) k];
                    b0[k][m] = sc.b0; b1[k][m] = sc.b1; a1[k][m] = sc.a1;
                }
            for (int m = 0; m < kM; ++m) gain[m] = d[(size_t) m].gain;
            fadeLen = std::max (1, (int) std::lround (kFadeSeconds * fs));
            reset();
            target = colorOn;
            mix = colorOn ? 1.0 : 0.0;
            fadePos = -1;
        }

        void reset() noexcept
        {
            for (auto& row : st) for (auto& v : row) v = 0.0;
        }

        void setColor (bool on) noexcept
        {
            if (on == target) return;
            target = on;
            fadeFrom = mix;
            fadePos = 0;
        }

        bool isColor() const noexcept { return target; }

        // y: Subtle output. from/to/w: the Subtle metal crossfade of this sample.
        double process (double y, int from, int to, double w) noexcept
        {
            double v[kM];
            for (int m = 0; m < kM; ++m) v[m] = y;
            for (int k = 0; k < kS; ++k)
            {
                double* __restrict s = st[k];
                const double* __restrict B0 = b0[k];
                const double* __restrict B1 = b1[k];
                const double* __restrict A1 = a1[k];
                for (int m = 0; m < kM; ++m)
                {
                    const double o = B0[m] * v[m] + s[m];
                    s[m] = B1[m] * v[m] - A1[m] * o;
                    v[m] = o;
                }
            }

            bool fading = false;
            if (fadePos >= 0)
            {
                const double t = (double) (fadePos + 1) / (double) fadeLen;
                mix = fadeFrom + ((target ? 1.0 : 0.0) - fadeFrom) * t;
                fading = true;
                if (++fadePos >= fadeLen) { fadePos = -1; mix = target ? 1.0 : 0.0; fading = false; }
            }
            if (! fading && ! target)
                return y;                                   // Subtle: bit-identical

            const double cf = v[from] * gain[from], ct = v[to] * gain[to];
            const double yc = (from == to) ? ct : cf + w * (ct - cf);
            return fading ? y + mix * (yc - y) : yc;
        }

    private:
        alignas (32) double b0[kS][kM] {}, b1[kS][kM] {}, a1[kS][kM] {}, st[kS][kM] {};
        double gain[kM] {};
        bool target = false;
        double mix = 0.0, fadeFrom = 0.0;
        int fadeLen = 1, fadePos = -1;
    };
}
