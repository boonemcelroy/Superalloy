/*
  ==============================================================================
    Superalloy — SkinShelf.h
    Realises the exact (Bessel) skin-effect magnitude of the selected metal as
    a minimum-phase high-shelf biquad, fitted at prepare time in the DIGITAL
    domain (so bilinear warping near Nyquist is included in the fit).
    Fit: minimax |dB error| over 20 Hz … min(0.45·fs, 100 kHz), multi-start
    Nelder–Mead. Typical max error 3e-6 … 2e-5 dB.
  ==============================================================================
*/
#pragma once

#include "TransformerDesign.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace superalloy
{
    class SkinShelf
    {
    public:
        struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

        void design (int metalIndex, double fs) noexcept
        {
            coeffs = Coeffs {};
            fitError = 0.0;
            const auto w = design::windingFor (metalIndex);
            if (w.fk <= 0.0) return; // superconductor: no skin effect

            const double fmax = std::min (0.45 * fs, 100000.0);
            constexpr int kGrid = 120;
            std::vector<double> f (kGrid), target (kGrid);
            for (int i = 0; i < kGrid; ++i)
            {
                f[(size_t) i] = 20.0 * std::pow (fmax / 20.0, (double) i / (kGrid - 1));
                target[(size_t) i] = 20.0 * std::log10 (design::skinRatioExact (metalIndex, f[(size_t) i]));
            }
            const double tEnd = target.back();
            if (std::abs (tEnd) < 1.0e-9) return; // effect below 1 nano-dB: identity

            auto cost = [&] (const double* p) -> double
            {
                const double f0 = std::exp (p[0]), Q = std::exp (p[2]);
                if (f0 > 0.49 * fs || f0 < 10.0 || Q < 0.05 || Q > 20.0) return 1.0e9;
                const Coeffs c = shelf (f0, p[1], Q, fs);
                double worst = 0.0;
                for (int i = 0; i < kGrid; ++i)
                    worst = std::max (worst, std::abs (magDB (c, f[(size_t) i], fs) - target[(size_t) i]));
                return worst;
            };

            double best[3] = { 0, 0, 0 }, bestCost = 1.0e30;
            const double fkLo = std::log (std::max (0.3 * w.fk, 50.0));
            const double fkHi = std::log (std::min (30.0 * w.fk, 0.45 * fs));
            for (int a = 0; a < 6; ++a)
                for (double q0 : { 0.45, 0.8 })
                {
                    double p[3] = { fkLo + (fkHi - fkLo) * a / 5.0, 2.0 * tEnd, std::log (q0) };
                    const double c = nelderMead (cost, p, std::abs (tEnd));
                    if (c < bestCost) { bestCost = c; std::copy (p, p + 3, best); }
                }
            coeffs = shelf (std::exp (best[0]), best[1], std::exp (best[2]), fs);
            fitError = bestCost;
        }

        void reset() noexcept { z1 = z2 = 0.0; }
        void copyStateFrom (const SkinShelf& o) noexcept { z1 = o.z1; z2 = o.z2; }

        double process (double x) noexcept
        {
            const double y = coeffs.b0 * x + z1;
            z1 = coeffs.b1 * x - coeffs.a1 * y + z2;
            z2 = coeffs.b2 * x - coeffs.a2 * y;
            return y;
        }

        double magnitudeAt (double hz, double fs) const noexcept { return std::pow (10.0, magDB (coeffs, hz, fs) / 20.0); }
        double getFitErrorDB() const noexcept { return fitError; }

    private:
        static Coeffs shelf (double f0, double gainDB, double Q, double fs) noexcept
        {
            // RBJ high shelf
            const double A = std::pow (10.0, gainDB / 40.0);
            const double w0 = 2.0 * design::kPi * f0 / fs;
            const double al = std::sin (w0) / (2.0 * Q), c = std::cos (w0), sA = std::sqrt (A);
            const double b0 = A * ((A + 1) + (A - 1) * c + 2 * sA * al);
            const double b1 = -2 * A * ((A - 1) + (A + 1) * c);
            const double b2 = A * ((A + 1) + (A - 1) * c - 2 * sA * al);
            const double a0 = (A + 1) - (A - 1) * c + 2 * sA * al;
            const double a1 = 2 * ((A - 1) - (A + 1) * c);
            const double a2 = (A + 1) - (A - 1) * c - 2 * sA * al;
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }

        static double magDB (const Coeffs& c, double hz, double fs) noexcept
        {
            const double w = 2.0 * design::kPi * hz / fs;
            const design::cd z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            const design::cd num = c.b0 + c.b1 * z1 + c.b2 * z2;
            const design::cd den = 1.0 + c.a1 * z1 + c.a2 * z2;
            return 20.0 * std::log10 (std::abs (num / den));
        }

        template <typename F>
        static double nelderMead (F& cost, double* p, double gainScale) noexcept
        {
            double s[4][3], fv[4];
            const double step[3] = { 0.5, std::max (gainScale, 1e-9), 0.3 };
            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 3; ++j) s[i][j] = p[j] + (i == j + 1 ? step[j] : 0.0);
                fv[i] = cost (s[i]);
            }
            for (int iter = 0; iter < 1500; ++iter)
            {
                int order[4] = { 0, 1, 2, 3 };
                std::sort (order, order + 4, [&] (int a, int b) { return fv[a] < fv[b]; });
                const int lo = order[0], hi = order[3], nh = order[2];
                if (fv[hi] - fv[lo] < 1.0e-13) break;
                double cen[3] = { 0, 0, 0 };
                for (int i = 0; i < 4; ++i) if (i != hi) for (int j = 0; j < 3; ++j) cen[j] += s[i][j] / 3.0;
                double xr[3], xe[3], xc[3];
                for (int j = 0; j < 3; ++j) xr[j] = cen[j] + (cen[j] - s[hi][j]);
                const double fr = cost (xr);
                if (fr < fv[lo])
                {
                    for (int j = 0; j < 3; ++j) xe[j] = cen[j] + 2.0 * (cen[j] - s[hi][j]);
                    const double fe = cost (xe);
                    if (fe < fr) { std::copy (xe, xe + 3, s[hi]); fv[hi] = fe; }
                    else         { std::copy (xr, xr + 3, s[hi]); fv[hi] = fr; }
                }
                else if (fr < fv[nh]) { std::copy (xr, xr + 3, s[hi]); fv[hi] = fr; }
                else
                {
                    for (int j = 0; j < 3; ++j) xc[j] = cen[j] + 0.5 * (s[hi][j] - cen[j]);
                    const double fc = cost (xc);
                    if (fc < fv[hi]) { std::copy (xc, xc + 3, s[hi]); fv[hi] = fc; }
                    else
                        for (int i = 0; i < 4; ++i)
                            if (i != lo)
                            {
                                for (int j = 0; j < 3; ++j) s[i][j] = s[lo][j] + 0.5 * (s[i][j] - s[lo][j]);
                                fv[i] = cost (s[i]);
                            }
                }
            }
            int b = 0; for (int i = 1; i < 4; ++i) if (fv[i] < fv[b]) b = i;
            std::copy (s[b], s[b] + 3, p);
            return fv[b];
        }

        Coeffs coeffs;
        double fitError = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };
}
