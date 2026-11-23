/*
  ==============================================================================
    Superalloy — designLevels.cpp
    Derives the Curie and Geiger geometries from Color, so the numbers in
    ColorContour.h (kLevels) are reproducible, not hand-picked.

    The ladder (owner): Subtle 1x, Color 4x, Curie 12x, Geiger 48x Subtle.
    So Curie is 3x Color and Geiger 12x Color.

    Color has two mechanisms, each with one geometry variable:
      low end   core factor k      (magnetising inductance / k)
      top end   foil layers m      (Dowell proximity term, m² − 1; thickness fixed 2.4 mm)
    For each new level both are scaled by the same factor s in dB re Color:
      max over metals |LF contour at 20 Hz|  = s x Color's   (René 41 / Inconel 625)
      max over metals |HF contour at 20 kHz| = s x Color's   (iridium)
    This keeps Color's balance between bass and treble. Then every metal is
    checked against the owner's cap for that level (±cap vs flat, and any two
    metals within cap), at 44.1 and 352.8 kHz, through the full digital design.
    If a cap were exceeded, s is trimmed (as Color's 4.0 was trimmed to 3.9).

    Usage: designLevels            (prints the derivation and the result)
  ==============================================================================
*/
#include "Channel630.h"
#include "ConsoleUtf8.h"

#include <cstdio>
#include <vector>

using namespace superalloy;
using cd = std::complex<double>;

namespace
{
    double dB (double x) { return 20.0 * std::log10 (x); }

    double lfDB (int m, double hz, double k) { return colorDesign::lfDB (m, hz, k); }

    colorDesign::Geometry geom (const char* name, double k, double layers, double cap, double s, bool exactHF)
    {
        return { name, k, colorDesign::kLevels[0].foilThickness, layers, cap, s, exactHF, exactHF ? 6 : 3 };
    }

    // deepest LF contour (any metal) at 20 Hz, and deepest HF contour (any metal) at 20 kHz
    double lfDepth (double k)
    {
        double d = 0; for (int m = 0; m < kNumMetals; ++m) d = std::max (d, -lfDB (m, 20.0, k)); return d;
    }
    double hfDepth (double layers, int* which = nullptr)
    {
        const auto g = geom ("", 1.0, layers, 1.0, 1.0, false);   // depth metric: foil alone (cores open)
        double d = 0;
        for (int m = 0; m < kNumMetals; ++m)
        {
            const double v = -colorDesign::hfTargetDB (m, 20000.0, g);
            if (v > d) { d = v; if (which) *which = m; }
        }
        return d;
    }

    template <typename F> double bisect (F f, double lo, double hi)   // f increasing, f(lo) < 0 < f(hi)
    {
        for (int i = 0; i < 200; ++i) { const double mid = 0.5 * (lo + hi); (f (mid) < 0 ? lo : hi) = mid; }
        return 0.5 * (lo + hi);
    }

    struct Report { double worstFlat, spread, spreadAt, fitErr, realErr, sepErr; };

    Report evaluate (const colorDesign::Geometry& g, bool print)
    {
        Report r {};
        for (double fs : { 44100.0, 352800.0 })
        {
            MetalBank bank; bank.prepare (fs);
            std::array<ColorDesign, kNumMetals> d;
            for (int m = 0; m < kNumMetals; ++m)
                d[(size_t) m].design (m, fs, [&bank, m] (double hz) { return bank.subtleMagnitude (m, hz); }, g);
            auto tot = [&] (int m, double f) { return dB (bank.subtleMagnitude (m, f) * d[(size_t) m].magnitudeAt (f, fs) * d[(size_t) m].gain); };
            for (int i = 0; i <= 600; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 600.0);
                double mx = -1e9, mn = 1e9;
                for (int m = 0; m < kNumMetals; ++m)
                {
                    const double v = tot (m, f);
                    mx = std::max (mx, v); mn = std::min (mn, v);
                    r.worstFlat = std::max (r.worstFlat, std::abs (v));
                    const cd s (0.0, 2.0 * design::kPi * design::warpedHz (f, fs));
                    const auto p = colorDesign::subtlePoles (m);
                    const double lf = std::abs ((s + p[0]) / (s + g.coreFactor * p[0]) * (s + p[1]) / (s + g.coreFactor * p[1]));
                    r.realErr = std::max (r.realErr, std::abs (dB (d[(size_t) m].magnitudeAt (f, fs)) - (dB (lf) + colorDesign::hfTargetDB (m, f, g))));
                    if (fs > 100000.0)
                        r.sepErr = std::max (r.sepErr, std::abs (lfDB (m, f, g.coreFactor) + colorDesign::hfTargetDB (m, f, g)
                                                                  - dB (std::abs (colorDesign::exactRatio (m, f, g)))));
                }
                if (mx - mn > r.spread) { r.spread = mx - mn; r.spreadAt = f; }
            }
            for (int m = 0; m < kNumMetals; ++m) r.fitErr = std::max (r.fitErr, d[(size_t) m].fitErrorDB);

            if (print && fs > 100000.0)
            {
                std::printf ("    %-46s %8s %8s %8s %8s %8s %8s %9s %9s %8s\n", "metal (352.8 kHz, dB re input)", "20 Hz", "50 Hz", "100 Hz",
                             "1 kHz", "5 kHz", "20 kHz", "100 kHz", "gain dB", "ph20Hz");
                for (int m = 0; m < kNumMetals; ++m)
                {
                    const cd s (0.0, 2.0 * design::kPi * 20.0);
                    const auto p = colorDesign::subtlePoles (m);
                    const double ph = std::arg ((s + p[0]) / (s + g.coreFactor * p[0]) * (s + p[1]) / (s + g.coreFactor * p[1])) * 180.0 / design::kPi;
                    std::printf ("    %-46s %+8.2f %+8.2f %+8.2f %+8.2f %+8.2f %+8.2f %+9.2f %+9.3f %+7.1f°\n", kMetals[(size_t) m].menuName,
                                 tot (m, 20), tot (m, 50), tot (m, 100), tot (m, 1000), tot (m, 5000), tot (m, 20000), tot (m, 100000),
                                 dB (d[(size_t) m].gain), ph);
                }
            }
        }
        return r;
    }
}

double round3 (double x) { const double e = std::pow (10.0, std::floor (std::log10 (x)) - 2.0); return std::round (x / e) * e; }

int main()
{
    superalloy::tools::useUtf8Console();
    const auto& color = colorDesign::kLevels[0];
    int irMetal = 0;
    const double lfRef = lfDepth (color.coreFactor), hfRef = hfDepth (color.foilLayers, &irMetal);
    std::printf ("Superalloy contour levels — derivation from Color\n\n");
    std::printf ("Color: core factor %.2f, foil %.1f mm x %.2f layers\n", color.coreFactor, color.foilThickness * 1e3, color.foilLayers);
    std::printf ("  deepest LF contour at 20 Hz  = %.4f dB (René 41 / Inconel 625)\n", lfRef);
    std::printf ("  deepest HF contour at 20 kHz = %.4f dB (%s, foil alone)\n", hfRef, kMetals[(size_t) irMetal].menuName);
    const auto rc = evaluate (color, false);
    std::printf ("  difference between any two metals %.4f dB at %.0f Hz (cap 1), vs flat %.3f dB, fit %.1e dB\n\n",
                 rc.spread, rc.spreadAt, rc.worstFlat, rc.fitErr);

    // Both mechanisms scaled by one factor s (dB re Color), solved so that the difference
    // between any two metals is n x Color's (owner: Curie 3x, Geiger 12x = 48x Subtle).
    auto geometryFor = [&] (const char* name, double s, double cap, double n)
    {
        const double k = bisect ([&] (double x) { return lfDepth (x) - s * lfRef; }, color.coreFactor, 5000.0);
        const double layers = bisect ([&] (double x) { return hfDepth (x) - s * hfRef; }, color.foilLayers, 500.0);
        return geom (name, k, layers, cap, n, true);
    };

    struct Target { const char* name; double n, cap; };
    for (const Target t : { Target { "Curie", 3.0, 3.0 }, Target { "Geiger", 12.0, 12.0 } })
    {
        const double want = t.n * rc.spread;
        std::printf ("%s: target difference between any two metals = %.0f x %.4f = %.4f dB (cap %.0f dB)\n", t.name, t.n, rc.spread, want, t.cap);
        double lo = 0.5 * t.n, hi = 2.0 * t.n;
        for (int it = 0; it < 18; ++it)
        {
            const double s = 0.5 * (lo + hi);
            const auto r = evaluate (geometryFor (t.name, s, t.cap, t.n), false);
            (r.spread < want ? lo : hi) = s;
        }
        const auto solved = geometryFor (t.name, 0.5 * (lo + hi), t.cap, t.n);
        const auto g = geom (t.name, round3 (solved.coreFactor), t.cap, t.n, t.n, true);
        auto gg = g; gg.foilLayers = round3 (solved.foilLayers); gg.capDB = t.cap; gg.intensity = t.n;
        std::printf ("  solved: s = %.4f x Color in each mechanism -> core factor %.6g, foil layers %.6g\n",
                     0.5 * (lo + hi), solved.coreFactor, solved.foilLayers);
        const auto r = evaluate (gg, false);
        std::printf ("  RESULT %s (rounded to 3 significant figures): core factor %.3g, foil %.1f mm x %.3g layers\n",
                     t.name, gg.coreFactor, gg.foilThickness * 1e3, gg.foilLayers);
        std::printf ("    LF depth %.3f dB (%.2f x Color), HF depth %.3f dB (%.2f x Color)\n",
                     lfDepth (gg.coreFactor), lfDepth (gg.coreFactor) / lfRef, hfDepth (gg.foilLayers), hfDepth (gg.foilLayers) / hfRef);
        std::printf ("    any two metals %.4f dB at %.0f Hz (%.3f x Color; cap %.0f), vs flat %.3f dB (cap ±%.0f)\n",
                     r.spread, r.spreadAt, r.spread / rc.spread, t.cap, r.worstFlat, t.cap);
        std::printf ("    HF fit error %.2e dB, realisation vs design %.2e dB, design vs exact network %.4f dB\n", r.fitErr, r.realErr, r.sepErr);
        evaluate (gg, true);
        std::printf ("\n");
    }
    return 0;
}
