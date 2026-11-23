/*
  ==============================================================================
    Superalloy — ColorContour.h
    The CONTOUR switch: Subtle (rev 6.1 sound, bit-identical) / Color / Curie /
    Geiger. Each step after Subtle is the same composite transformer with the
    same two geometry variables pushed further. Every metal is still wound
    identically; the metal still enters ONLY through rho.

      1. Core: magnetising inductance divided by coreFactor
         -> every metal's two low-frequency poles move coreFactor x higher.
            High-resistivity metals tighten the deep bass; the pure metals
            (low winding resistance) are almost untouched.
      2. Winding: foil of thickness foilThickness with foilLayers layers
         -> the proximity effect (Dowell's multilayer eddy-current solution)
            raises each winding's AC resistance where the skin depth of that
            metal falls below the foil thickness. The thickness sets WHERE it
            starts (fixed at 2.4 mm for every level, so each metal's top end
            starts easing at the same frequency in every level); the layer
            count sets HOW MUCH (Dowell's proximity term grows as m² − 1).

      Level   coreFactor   foil layers   cap (owner)      intensity re Color
      Color      3.9          2           ±1 dB            1x   (4x Subtle)
      Curie      see kLevels  see kLevels ±3 dB            3x   (12x Subtle)
      Geiger     see kLevels  see kLevels ±12 dB           12x  (48x Subtle)

      Curie and Geiger are solved (tools/designLevels.cpp) so that BOTH
      mechanisms are scaled by the same factor in dB re Color: the deepest
      low-frequency contour of any metal at 20 Hz and the deepest
      high-frequency contour of any metal at 20 kHz are 3x (Curie) and 12x
      (Geiger) Color's. Color's balance between bass and treble is kept.
      The cap applies like Color's: every metal within ±cap of flat, and any
      two metals within cap of each other, 20 Hz – 20 kHz.
      The layer count is an effective count: it enters Dowell's solution only
      through m², so a non-integer value is a well-defined winding between two
      integer ones (as the virtual turns ratio N = 4.566 is).

    Realisation (why distortion does not change):
      Level output = Subtle engine (unchanged, nonlinear) -> per-metal LINEAR
      contour filter = (level network) / (Subtle network). The contour adds no
      nonlinearity. Its factors:
        • low frequencies: exact pole/zero ratio Π (s+p_i)/(s+k·p_i), k = coreFactor, where p_i
          are the Subtle network's two magnetising poles (closed form)
        • high frequencies: Dowell foil response divided by the round-wire
          skin response Subtle already contains; realised as three cascaded
          first-order minimum-phase sections fitted in the DIGITAL domain at
          prepare time (warping included), bounded Levenberg–Marquardt on the
          dB error; in-band error checked by the harness.
      Each metal's output is level-matched to the input on pink noise
      band-limited 20 Hz – 20 kHz, in every level (owner ruling: ≤ 0.01 dB).

    Switching: every contour of every level runs continuously on the engine
    output (3 levels x 15 metals), so a metal change or a level change is a
    pure 40 ms equal-gain crossfade between signals that are already settled.
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

        struct Geometry
        {
            const char* name;
            double coreFactor;      // magnetising inductance divided by this
            double foilThickness;   // m
            double foilLayers;      // Dowell layer count (effective; enters only as m²)
            double capDB;           // owner ruling: |dev| vs flat and between any two metals, 20 Hz – 20 kHz
            double intensity;       // difference between metals re Color's (1x, 3x, 12x)
            bool   exactHF;         // HF target from the exact network, extended fitter (see hfTargetDB, fitHF)
            int    hfSections;      // first-order sections realising the HF factor
        };

        inline constexpr int kNumLevels = 3;   // Color, Curie, Geiger (Subtle has no contour)

        inline constexpr std::array<Geometry, kNumLevels> kLevels {{
            // Color: owner 4x Subtle; 4.0 trimmed to 3.9 to hold 1 dB between any two metals
            { "Color",  3.9,                2.4e-3, 2.0,                1.0,  1.0,  false, 3 },
            // Curie: owner 3x Color (~3 dB). Solved by tools/designLevels.cpp
            { "Curie",  7.64,               2.4e-3, 3.36,               3.0,  3.0,  true,  6 },
            // Geiger: owner 12 dB, 48x Subtle = 12x Color. Solved by tools/designLevels.cpp
            { "Geiger", 27.6,               2.4e-3, 7.52,               12.0, 12.0, true,  6 },
        }};

        // ColorStage stores levels in order of section count and skips a section
        // for the leading levels that do not have it.
        static_assert (kLevels[0].hfSections <= kLevels[1].hfSections && kLevels[1].hfSections <= kLevels[2].hfSections,
                       "levels must be ordered by HF section count");
        static_assert (kLevels[2].hfSections <= 6, "ColorDesign::kHFMax");

        // Dowell: Z_ac / R_dc of an m-layer foil winding of thickness h.
        inline cd dowellZn (double hz, double rho, const Geometry& g) noexcept
        {
            if (rho <= 0.0 || hz <= 0.0) return cd (1.0, 0.0);
            const double delta = std::sqrt (rho / (kPi * hz * kMu0));
            const cd x = cd (1.0, 1.0) * (g.foilThickness / delta);
            const double m = g.foilLayers;
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

        // Exact level/Subtle ratio of the full networks (harness reference).
        inline cd exactRatio (int metal, double hz, const Geometry& g) noexcept
        {
            const auto w = design::windingFor (metal);
            const double rho = kMetals[(size_t) metal].rho;
            const cd zf = dowellZn (hz, rho, g), zs = design::skinZn (hz, w.fk);
            const cd level  = network (w.Rp * zf, (w.Rs + w.Rp) * zf, w.Rs * zf, design::kLmInitial / g.coreFactor, hz);
            const cd subtle = network (w.Rp * zs, (w.Rs + w.Rp) * zs, w.Rs * zs, design::kLmInitial, hz);
            return level / subtle;
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

        // Low-frequency factor, analog: Π (s + p_i) / (s + k·p_i), exact (time constants scale as L).
        inline double lfDB (int metal, double hz, double k) noexcept
        {
            const auto p = subtlePoles (metal);
            const cd s (0.0, 2.0 * kPi * hz);
            return 20.0 * std::log10 (std::abs ((s + p[0]) / (s + k * p[0]) * (s + p[1]) / (s + k * p[1])));
        }

        // High-frequency factor.
        //  Color (exactHF = false): cores open, series winding string into RL. Its
        //    product with the LF factor is within 0.006 dB of the exact network.
        //  Curie / Geiger (exactHF = true): the exact level/Subtle network ratio
        //    divided by the exact LF factor. At these intensities the core shunt and
        //    the foil's AC impedance interact (the open-core shortcut is 0.03 dB off
        //    at Curie and 0.36 dB at Geiger), so the fit targets the exact network.
        inline double hfTargetDB (int metal, double hz, const Geometry& g) noexcept
        {
            if (g.exactHF)
                return 20.0 * std::log10 (std::abs (exactRatio (metal, hz, g))) - lfDB (metal, hz, g.coreFactor);
            const auto w = design::windingFor (metal);
            const double Rt = w.Rtotal();
            const cd zf = dowellZn (hz, kMetals[(size_t) metal].rho, g), zs = design::skinZn (hz, w.fk);
            return 20.0 * std::log10 (std::abs ((design::kRLoad + Rt * zs) / (design::kRLoad + Rt * zf)));
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
        static constexpr int kLF = 2, kHFMax = 6, kMaxSections = kLF + kHFMax;
        std::array<FirstOrder, kMaxSections> sec {};    // unused sections stay identity (exactly 1)
        int numSections = kLF;
        double gain = 1.0;          // pink-noise level match
        double fitErrorDB = 0.0;    // in-band (20 Hz – 20 kHz) worst error of the HF fit

        double magnitudeAt (double hz, double fs) const noexcept   // without gain
        {
            const double cw = std::cos (2.0 * design::kPi * hz / fs);
            double m2 = 1.0;
            for (const auto& s : sec) m2 *= s.mag2 (cw);
            return std::sqrt (m2);
        }

        // subtleMag(hz): magnitude of the Subtle chain (engine + skin shelf + makeup)
        void design (int metal, double fs, const std::function<double (double)>& subtleMag, const colorDesign::Geometry& g)
        {
            using namespace colorDesign;
            sec.fill (FirstOrder {});
            fitErrorDB = 0.0;
            numSections = kLF + g.hfSections;

            // 1. low frequencies: exact
            const auto p = subtlePoles (metal);
            for (int i = 0; i < kLF; ++i)
                sec[(size_t) i] = FirstOrder::make (p[(size_t) i], g.coreFactor * p[(size_t) i], 1.0, fs);

            // 2. high frequencies: fit of first-order sections
            if (g.exactHF) fitHF<6, 160, true> (metal, fs, g);
            else           fitHF<3, 140, false> (metal, fs, g);     // Color: the rev 6.2 fitter, unchanged

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
        // the weighted dB error, several starts, best kept.
        //
        //  Color (Extended = false, 3 sections): grid 10 Hz – 0.45 fs, corners
        //    ≥ 2 Hz, three treble starts. Exactly the rev 6.2 fitter.
        //  Curie / Geiger (Extended = true, 6 sections): their exact-network
        //    targets also step at low frequency (the foil's internal inductance
        //    against the reduced magnetising inductance: below 20 Hz for pure
        //    metals, 100 Hz – 2 kHz for superalloys). So the grid runs 1 Hz –
        //    0.49 fs (weight 0.3 below 20 Hz, 1 to 22.05 kHz, 0.1 above), corners down to 0.05 Hz,
        //    and the first start is shape-informed: corners log-spaced 2 Hz –
        //    fmax, each section taking the target's change across its band.
        template <int NHF, int G, bool Extended>
        void fitHF (int metal, double fs, const colorDesign::Geometry& g)
        {
            using namespace colorDesign;
            constexpr int D = 2 * NHF;
            const double fmax = std::min ((Extended ? 0.49 : 0.45) * fs, 100000.0);
            const double fFirst = Extended ? 1.0 : 10.0;
            const double K = 2.0 * fs;
            std::array<double, (size_t) G> W2 {}, tgt {}, wt {}, hzv {};
            for (int i = 0; i < G; ++i)
            {
                hzv[(size_t) i] = fFirst * std::pow (fmax / fFirst, (double) i / (G - 1));
                const double Om = K * std::tan (kPi * hzv[(size_t) i] / fs);
                W2[(size_t) i] = Om * Om;
                tgt[(size_t) i] = hfTargetDB (metal, hzv[(size_t) i], g);
                // Extended: full weight up to 22.05 kHz, so the 20 kHz band edge is inside the
                // fully weighted region at every rate (at 44.1 kHz the grid ends at 21.6 kHz).
                wt[(size_t) i] = hzv[(size_t) i] <= (Extended ? 22050.0 : 20000.0)
                                   ? ((Extended && hzv[(size_t) i] < 20.0) ? 0.3 : 1.0) : 0.1;
            }
            if (std::abs (tgt[(size_t) G - 1]) < 1.0e-9) return;     // superconductor: identity

            constexpr double c10 = 10.0 / 2.302585092994046;
            const double lo = std::log (2.0 * kPi * (Extended ? 0.05 : 2.0)), hi = std::log (K * 50.0);

            // q = { ln p0, ln z0, ln p1, ln z1, ... } (warped rad/s)
            auto evalAll = [&] (const double* q, double* r, double (*J)[(size_t) D]) noexcept
            {
                for (int i = 0; i < G; ++i)
                {
                    double y = 0.0;
                    for (int k = 0; k < NHF; ++k)
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

            double best[(size_t) D] {}, bestCost = 1.0e300;
            auto tryStart = [&] (double* q)
            {
                const double c = levenbergMarquardt<D, G> (evalAll, q, lo, hi, Extended ? 400 : 250);
                if (c < bestCost) { bestCost = c; std::copy (q, q + D, best); }
            };
            if constexpr (Extended)
            {
                double q[(size_t) D];
                const double f0 = 2.0;
                for (int k = 0; k < NHF; ++k)
                {
                    const double fa = f0 * std::pow (fmax / f0, (double) k / NHF), fb = f0 * std::pow (fmax / f0, (double) (k + 1) / NHF);
                    double depth = hfTargetDB (metal, fb, g) - hfTargetDB (metal, fa, g);   // dB change across the band
                    if (std::abs (depth) < 1.0e-3) depth = -1.0e-3;
                    const double pw = 2.0 * kPi * std::sqrt (fa * fb);
                    q[2 * k]     = std::clamp (std::log (pw), lo, hi);
                    q[2 * k + 1] = std::clamp (std::log (pw * std::pow (10.0, -depth / 20.0)), lo, hi);
                }
                tryStart (q);
                for (double base : { 1.0, 10.0 })
                {
                    for (int k = 0; k < NHF; ++k)
                    {
                        const double fp = base * std::pow (5.0, k);
                        q[2 * k]     = std::clamp (std::log (2.0 * kPi * fp), lo, hi);
                        q[2 * k + 1] = std::clamp (std::log (2.0 * kPi * fp * 1.5), lo, hi);
                    }
                    tryStart (q);
                }
            }
            else
            {
                for (double base : { 700.0, 2000.0, 5000.0 })
                {
                    double q[(size_t) D];
                    for (int k = 0; k < NHF; ++k)
                    {
                        const double fp = base * std::pow (4.0, k);
                        q[2 * k]     = std::clamp (std::log (2.0 * kPi * fp), lo, hi);
                        q[2 * k + 1] = std::clamp (std::log (2.0 * kPi * fp * 1.5), lo, hi);
                    }
                    tryStart (q);
                }
            }

            double r[(size_t) G];
            evalAll (best, r, nullptr);
            double inband = 0.0;
            for (int i = 0; i < G; ++i)
                if (hzv[(size_t) i] >= (Extended ? 20.0 : 0.0) && hzv[(size_t) i] <= 20000.0)
                    inband = std::max (inband, std::abs (r[i] / wt[(size_t) i]));
            fitErrorDB = inband;

            for (int k = 0; k < NHF; ++k)
            {
                const double p = std::exp (best[2 * k]), z = std::exp (best[2 * k + 1]);
                FirstOrder f;
                const double gk = p / z;                      // unity at DC
                f.b0 = gk * (K + z) / (K + p);
                f.b1 = gk * (z - K) / (K + p);
                f.a1 = (p - K) / (K + p);
                sec[(size_t) (kLF + k)] = f;
            }
        }

        template <int D, int G, typename F>
        static double levenbergMarquardt (F& evalAll, double* q, double lo, double hi, int maxIter) noexcept
        {
            double r[(size_t) G], rt[(size_t) G], J[(size_t) G][(size_t) D];
            auto sumsq = [] (const double* v) { double s = 0; for (int i = 0; i < G; ++i) s += v[i] * v[i]; return s; };
            evalAll (q, r, J);
            double cost = sumsq (r), lambda = 1.0e-3;
            for (int iter = 0; iter < maxIter; ++iter)
            {
                double A[(size_t) D][(size_t) D] = {}, g[(size_t) D] = {};
                for (int i = 0; i < G; ++i)
                    for (int a = 0; a < D; ++a)
                    {
                        g[a] += J[i][a] * r[i];
                        for (int b = 0; b < D; ++b) A[a][b] += J[i][a] * J[i][b];
                    }
                bool improved = false;
                for (int tries = 0; tries < 16 && ! improved; ++tries)
                {
                    double M[(size_t) D][(size_t) D + 1];
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
                    double qn[(size_t) D];
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
    // Per-channel contour stage. Runs every contour of every level continuously
    // (3 levels x 15 metals = 45 filters). Structure-of-arrays: each section is
    // processed for all 45 at once, so the inner loop vectorises (they are
    // independent). Position 0 = Subtle (no contour), 1 = Color, 2 = Curie,
    // 3 = Geiger. A change of position, also in the middle of a fade, is a
    // 40 ms equal-gain crossfade between outputs that are already settled.
    class ColorStage
    {
    public:
        static constexpr double kFadeSeconds = 0.040;
        static constexpr int kL = colorDesign::kNumLevels, kM = kNumMetals, kC = kL * kM, kS = ColorDesign::kMaxSections;
        static constexpr int kPositions = kL + 1;
        using Designs = std::array<std::array<ColorDesign, kNumMetals>, colorDesign::kNumLevels>;

        void prepare (const Designs& d, double fs, int position) noexcept
        {
            for (int L = 0; L < kL; ++L)
                for (int m = 0; m < kM; ++m)
                {
                    const int c = L * kM + m;
                    for (int k = 0; k < kS; ++k)
                    {
                        const auto& sc = d[(size_t) L][(size_t) m].sec[(size_t) k];
                        b0[k][c] = sc.b0; b1[k][c] = sc.b1; a1[k][c] = sc.a1;
                    }
                    gain[c] = d[(size_t) L][(size_t) m].gain;
                }
            // Section k runs only for contours that have it. Levels are stored in order of
            // section count (Color 5, Curie 8, Geiger 8), so that is a contiguous tail.
            for (int k = 0; k < kS; ++k)
            {
                firstContour[k] = kC;
                for (int L = kL - 1; L >= 0; --L)
                    if (d[(size_t) L][0].numSections > k) firstContour[k] = L * kM;
                    else break;
            }
            fadeLen = std::max (1, (int) std::lround (kFadeSeconds * fs));
            reset();
            target = std::clamp (position, 0, kL);
            for (int p = 0; p < kPositions; ++p) weight[p] = fadeFrom[p] = (p == target ? 1.0 : 0.0);
            fadePos = -1;
        }

        void reset() noexcept
        {
            for (auto& row : st) for (auto& v : row) v = 0.0;
        }

        void setPosition (int position) noexcept
        {
            position = std::clamp (position, 0, kL);
            if (position == target) return;
            target = position;
            for (int p = 0; p < kPositions; ++p) fadeFrom[p] = weight[p];   // also mid-fade: start from where we are
            fadePos = 0;
        }

        int getPosition() const noexcept { return target; }

        // y: Subtle output. from/to/w: the Subtle metal crossfade of this sample.
        double process (double y, int from, int to, double w) noexcept
        {
            double v[kC];
            for (int c = 0; c < kC; ++c) v[c] = y;
            for (int k = 0; k < kS; ++k)
            {
                double* __restrict s = st[k];
                const double* __restrict B0 = b0[k];
                const double* __restrict B1 = b1[k];
                const double* __restrict A1 = a1[k];
                for (int c = firstContour[k]; c < kC; ++c)
                {
                    const double o = B0[c] * v[c] + s[c];
                    s[c] = B1[c] * v[c] - A1[c] * o;
                    v[c] = o;
                }
            }

            if (fadePos < 0)
                return output (target, y, v, from, to, w);          // settled; Subtle returns y itself

            const double t = (double) (fadePos + 1) / (double) fadeLen;
            if (++fadePos >= fadeLen)
            {
                fadePos = -1;
                for (int p = 0; p < kPositions; ++p) weight[p] = (p == target ? 1.0 : 0.0);
                return output (target, y, v, from, to, w);
            }
            double out = 0.0;
            for (int p = 0; p < kPositions; ++p)
            {
                weight[p] = fadeFrom[p] + ((p == target ? 1.0 : 0.0) - fadeFrom[p]) * t;
                if (weight[p] > 0.0)
                    out += weight[p] * output (p, y, v, from, to, w);
            }
            return out;
        }

    private:
        double output (int position, double y, const double* v, int from, int to, double w) const noexcept
        {
            if (position == 0)
                return y;                                           // Subtle: bit-identical
            const int base = (position - 1) * kM;
            const double cf = v[base + from] * gain[base + from], ct = v[base + to] * gain[base + to];
            return (from == to) ? ct : cf + w * (ct - cf);
        }

        // No alignas: a 32-byte alignment would only align row 0 of each array and
        // forces MSVC to pad between arrays (C4324).
        double b0[kS][kC] {}, b1[kS][kC] {}, a1[kS][kC] {}, st[kS][kC] {};
        double gain[kC] {};
        double weight[kPositions] {}, fadeFrom[kPositions] {};
        int firstContour[kS] {};
        int target = 0;
        int fadeLen = 1, fadePos = -1;
    };
}
