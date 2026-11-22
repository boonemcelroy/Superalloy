/*
  ==============================================================================
    Superalloy — verifySuperalloy.cpp
    Stand-alone verification harness. Uses the exact plugin DSP sources
    (TransformerEngine + Channel630), no JUCE. Every owner ruling is tested.

    Usage:  verifySuperalloy              run all tests, write report to stdout
            verifySuperalloy --calibrate  solve the core shape constant u_r so the
                                   copper cascade gives 0.002 % THD at
                                   20 Hz / −20 dBFS (+4 dBu), then exit
            verifySuperalloy --quick      fewer metals/frequencies (smoke test)
  ==============================================================================
*/
#include "../Source/Channel630.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <complex>
#include <random>
#include <algorithm>

#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

using namespace superalloy;
using cd = std::complex<double>;

static int g_failures = 0;

static const MetalBank& bankFor (double fs)
{
    static std::vector<std::pair<double, MetalBank*>> cache;
    for (auto& c : cache) if (c.first == fs) return *c.second;
    auto* b = new MetalBank(); b->prepare (fs);
    cache.push_back ({ fs, b });
    return *b;
}
static void check (bool ok, const char* what, const char* detail)
{
    std::printf ("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (! ok) ++g_failures;
}

static double dB (double x) { return 20.0 * std::log10 (std::max (x, 1e-300)); }

//==============================================================================
// Blackman-Harris 4-term windowed single-bin DFT
static cd windowedBin (const std::vector<double>& x, double f, double fs)
{
    const size_t N = x.size();
    const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    cd acc = 0.0;
    double wsum = 0.0;
    for (size_t n = 0; n < N; ++n)
    {
        const double t = 2.0 * design::kPi * (double) n / (double) (N - 1);
        const double w = a0 - a1 * std::cos (t) + a2 * std::cos (2 * t) - a3 * std::cos (3 * t);
        const double ph = -2.0 * design::kPi * f * (double) n / fs;
        acc += w * x[n] * cd (std::cos (ph), std::sin (ph));
        wsum += w;
    }
    return 2.0 * acc / wsum;
}

struct SineRun { std::vector<double> in, out; double peakU = 0.0; };

static SineRun runSine (Channel630& ch, double fs, double f, double dBFS,
                        double settleSec, double measSec, double minCycles = 40.0)
{
    SineRun r;
    const double A = std::pow (10.0, dBFS / 20.0);
    const double meas = std::max (measSec, minCycles / f);
    const size_t nSettle = (size_t) std::llround (settleSec * fs);
    const size_t nMeas = (size_t) std::llround (meas * fs);
    r.in.reserve (nMeas); r.out.reserve (nMeas);
    for (size_t n = 0; n < nSettle + nMeas; ++n)
    {
        if (n == nSettle) ch.getActiveEngine().clearPeakCoreU();
        const double x = A * std::cos (2.0 * design::kPi * f * (double) n / fs); // cosine start: no flux offset
        const double y = ch.process (x);
        if (n >= nSettle) { r.in.push_back (x); r.out.push_back (y); }
    }
    r.peakU = ch.getActiveEngine().getPeakCoreU();
    return r;
}

static double thdOf (const std::vector<double>& y, double f, double fs, int H = 10)
{
    const double f1 = std::abs (windowedBin (y, f, fs));
    double s = 0.0;
    for (int h = 2; h <= H; ++h)
    {
        if (h * f >= 0.5 * fs) break;
        const double a = std::abs (windowedBin (y, h * f, fs));
        s += a * a;
    }
    return std::sqrt (s) / f1;
}

// THD+N by least-squares sine fit at known f (includes aliases and noise)
static double thdnFit (const std::vector<double>& y, double f, double fs)
{
    double sc = 0, ss = 0, cc = 0, yc = 0, ys = 0, sy = 0;
    const size_t N = y.size();
    for (size_t n = 0; n < N; ++n)
    {
        const double ph = 2.0 * design::kPi * f * (double) n / fs;
        const double c = std::cos (ph), s = std::sin (ph);
        cc += c * c; ss += s * s; sc += s * c; yc += y[n] * c; ys += y[n] * s; sy += y[n];
    }
    const double det = cc * ss - sc * sc;
    const double a = (yc * ss - ys * sc) / det, b = (ys * cc - yc * sc) / det;
    const double mean = sy / (double) N;
    double res = 0, fund = 0;
    for (size_t n = 0; n < N; ++n)
    {
        const double ph = 2.0 * design::kPi * f * (double) n / fs;
        const double m = a * std::cos (ph) + b * std::sin (ph);
        res += (y[n] - m - mean) * (y[n] - m - mean);
        fund += m * m;
    }
    return std::sqrt (res / fund);
}

static double gainAt (Channel630& ch, double fs, double f, double dBFS)
{
    auto r = runSine (ch, fs, f, dBFS, 1.0, 0.25, 40.0);
    return std::abs (windowedBin (r.out, f, fs)) / std::abs (windowedBin (r.in, f, fs));
}

static double copperTHD20 (double fs)
{
    Channel630 ch; ch.prepare (bankFor (fs), 2);
    auto r = runSine (ch, fs, 20.0, -20.0, 2.0, 2.0, 40.0);
    return thdOf (r.out, 20.0, fs);
}

// Paul Kellet's refined pink filter on Gaussian white noise, normalised.
static std::vector<double> pinkNoise (size_t N, double rmsDBFS, unsigned seed)
{
    std::mt19937_64 rng (seed);
    std::normal_distribution<double> g (0.0, 1.0);
    std::vector<double> p (N);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (size_t n = 0; n < N; ++n)
    {
        const double w = g (rng);
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        p[n] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
    }
    double s = 0; for (double v : p) s += v * v;
    const double k = std::pow (10.0, rmsDBFS / 20.0) / std::sqrt (s / (double) N);
    for (double& v : p) v *= k;
    return p;
}

// 2nd-order Butterworth high-pass 20 Hz + low-pass 20 kHz (bilinear), in place
static void bandLimit (std::vector<double>& x, double fs)
{
    auto run = [&] (bool hp, double fc)
    {
        const double K = std::tan (design::kPi * fc / fs), q = std::sqrt (0.5);
        const double n = 1.0 / (1.0 + K / q + K * K);
        double b0, b1, b2;
        if (hp) { b0 = n; b1 = -2 * n; b2 = n; } else { b0 = K * K * n; b1 = 2 * b0; b2 = b0; }
        const double a1 = 2 * (K * K - 1) * n, a2 = (1 - K / q + K * K) * n;
        double z1 = 0, z2 = 0;
        for (double& v : x) { const double y = b0 * v + z1; z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y; v = y; }
    };
    run (true, 20.0); run (false, 20000.0);
}

static double rmsOf (const std::vector<double>& v, size_t from)
{
    double s = 0; for (size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
    return std::sqrt (s / (double) (v.size() - from));
}

//==============================================================================
static void calibrate()
{
    const double fs = 352800.0;
    const double target = 2.0e-5;
    double lo = std::log (0.002), hi = std::log (2.0);
    for (int i = 0; i < 40; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        TransformerEngine::coreUr = std::exp (mid);
        const double t = copperTHD20 (fs);
        if (t > target) lo = mid; else hi = mid; // smaller u_r → more distortion
    }
    TransformerEngine::coreUr = std::exp (0.5 * (lo + hi));
    const double t = copperTHD20 (fs);
    std::printf ("CALIBRATION  u_r = %.10g   copper THD @20 Hz -20 dBFS = %.6f %%\n",
                 TransformerEngine::coreUr, t * 100.0);
}

//==============================================================================
int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
    const bool quick = argc > 1 && std::strcmp (argv[1], "--quick") == 0;
    if (argc > 1 && std::strcmp (argv[1], "--calibrate") == 0) { calibrate(); return 0; }

    std::printf ("Superalloy verification harness\n");
    std::printf ("core u_r = %.10g, flux knee = %.6f V*s, virtual turns ratio N = %.4f, V(0 dBFS) = %.5f Vpk\n\n",
                 TransformerEngine::coreUr, design::fluxKnee(), design::virtualTurnsRatio(), design::kVoltsAtFullScale);

    const double FS_HI = 352800.0, FS_LO = 44100.0;
    std::vector<int> metals;
    for (int m = 0; m < kNumMetals; ++m) metals.push_back (m);
    if (quick) metals = { 0, 2, 7, 14 };

    //--------------------------------------------------------------------------
    std::printf ("A. DESIGN TABLE (analog, small-signal)\n");
    std::printf ("  %-46s %9s %9s %9s %10s %9s %11s %11s\n", "metal", "Rp ohm", "Rs ohm", "skin fk", "makeup dB", "LF-3dB Hz", "fit 44.1k", "fit 352.8k");
    for (int m : metals)
    {
        const auto w = design::windingFor (m);
        const double g1k = std::abs (design::networkTransfer (m, 1000.0));
        double lo = 1e-5, hi = 100.0;
        for (int i = 0; i < 200; ++i)
        {
            const double mid = std::sqrt (lo * hi);
            if (std::abs (design::networkTransfer (m, mid)) / g1k < std::pow (10.0, -3.0 / 20.0)) lo = mid; else hi = mid;
        }
        std::printf ("  %-46s %9.4f %9.4f %9.0f %10.4f %9.4f %11.2e %11.2e\n", kMetals[(size_t) m].menuName,
                     w.Rp, w.Rs, w.fk, dB (bankFor (FS_HI).makeup[(size_t) m]), std::sqrt (lo * hi),
                     bankFor (FS_LO).shelf[(size_t) m].getFitErrorDB(), bankFor (FS_HI).shelf[(size_t) m].getFitErrorDB());
    }
    {
        // Proof that the analytic-only parasitics (leakage, winding C, conductor internal
        // inductance) are inaudible: full composite vs the real-time model, 20 Hz - 20 kHz.
        double worst = 0, worstF = 0; int worstM = 0;
        for (int m = 0; m < kNumMetals; ++m)
            for (int i = 0; i <= 400; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 400.0);
                const double full = std::abs (design::fullCompositeTransfer (m, f, true));
                const double rt = std::abs (design::networkTransfer (m, f)) * design::skinRatioExact (m, f);
                const double d = std::abs (dB (full / rt));
                if (d > worst) { worst = d; worstF = f; worstM = m; }
            }
        char b[160]; std::snprintf (b, sizeof b, "worst %.2e dB at %.0f Hz (%s); limit 1e-4 dB",
                                    worst, worstF, kMetals[(size_t) worstM].menuName);
        check (worst <= 1e-4, "Omitted MHz parasitics are inaudible", b);

        const double g1k = std::abs (design::fullCompositeTransfer (2, 1000.0, true));
        double f = 1e5;
        while (f < 1e9 && std::abs (design::fullCompositeTransfer (2, f, true)) / g1k > std::pow (10.0, -3.0 / 20.0)) f *= 1.001;
        std::snprintf (b, sizeof b, "first -3 dB crossing %.2f MHz incl. exaggerated skin (target 15 MHz +/-10%%)", f / 1e6);
        check (std::abs (f / 15e6 - 1.0) < 0.10, "Copper composite HF bandwidth", b);
        const double ph = std::arg (design::fullCompositeTransfer (2, 20000.0, true) / design::fullCompositeTransfer (2, 20000.0, false));
        std::printf ("  note: leakage/C group delay at 20 kHz = %.1f ns (analytic, not simulated)\n", -ph / (2 * design::kPi * 20000.0) * 1e9);
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("B. LEVEL: 1 kHz, -20 dBFS, every metal, %s\n", "44.1 kHz and 352.8 kHz");
    for (double fs : { FS_LO, FS_HI })
    {
        double worst = 0;
        for (int m : metals)
        {
            Channel630 ch; ch.prepare (bankFor (fs), m);
            worst = std::max (worst, std::abs (dB (gainAt (ch, fs, 1000.0, -20.0))));
        }
        char b[96]; std::snprintf (b, sizeof b, "fs %.1f kHz: worst |level error| = %.6f dB (limit 0.001)", fs / 1000.0, worst);
        check (worst <= 0.001, "Static makeup", b);
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    const std::vector<double> freqs = quick ? std::vector<double> { 20, 1000, 20000 }
                                            : std::vector<double> { 20, 31.5, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000 };
    for (double fs : { FS_HI, FS_LO })
    {
        std::printf ("C. RESPONSE vs COPPER, -20 dBFS sine, fs %.1f kHz (dB; owner cap +/-0.1)\n", fs / 1000.0);
        std::vector<double> cu;
        { Channel630 ch; ch.prepare (bankFor (fs), 2); for (double f : freqs) cu.push_back (gainAt (ch, fs, f, -20.0)); }
        std::printf ("  %-46s", "metal \\ Hz");
        for (double f : freqs) std::printf (" %8g", f);
        std::printf ("\n");
        double worst = 0;
        for (int m : metals)
        {
            Channel630 ch; ch.prepare (bankFor (fs), m);
            std::printf ("  %-46s", kMetals[(size_t) m].menuName);
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                const double d = dB (gainAt (ch, fs, freqs[i], -20.0) / cu[i]);
                worst = std::max (worst, std::abs (d));
                std::printf (" %8.4f", d);
            }
            std::printf ("\n");
        }
        char b[96]; std::snprintf (b, sizeof b, "worst |deviation from copper| = %.4f dB", worst);
        check (worst <= 0.1, "Shape cap 20 Hz-20 kHz", b);
        std::printf ("\n");
    }

    //--------------------------------------------------------------------------
    std::printf ("D. ENGINE vs EXACT CIRCUIT EQUATIONS (-40 dBFS, 1-20 kHz, every metal, both rates)\n");
    {
        double worst = 0;
        for (double fs : { FS_LO, FS_HI })
            for (int m : metals)
                for (double f : { 1000.0, 5000.0, 10000.0, 15000.0, 20000.0 })
                {
                    Channel630 ch; ch.prepare (bankFor (fs), m);
                    const double meas = gainAt (ch, fs, f, -40.0);
                    const double pred = std::abs (design::networkTransfer (m, design::warpedHz (f, fs)))
                                      * bankFor (fs).makeup[(size_t) m] * design::skinRatioExact (m, f);
                    worst = std::max (worst, std::abs (dB (meas / pred)));
                }
        char b[96]; std::snprintf (b, sizeof b, "worst mismatch %.2e dB (limit 1e-4)", worst);
        check (worst <= 1e-4, "Engine + skin shelf match the exact Bessel/circuit solution", b);
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("E. DISTORTION (THD %%, harmonics 2-10), fs 352.8 kHz\n");
    {
        const double cuT = copperTHD20 (FS_HI);
        char b[96]; std::snprintf (b, sizeof b, "copper %.5f %% (target 0.002 %% +/-5%%)", cuT * 100);
        check (std::abs (cuT / 2e-5 - 1.0) < 0.05, "Calibration: 20 Hz, -20 dBFS (+4 dBu)", b);

        const std::vector<double> tf = quick ? std::vector<double> { 20, 100 } : std::vector<double> { 20, 50, 100, 1000 };
        const std::vector<double> tl = { -20.0, -6.0, 0.0 };
        std::printf ("  %-46s", "metal \\ (Hz @ dBFS)");
        for (double l : tl) for (double f : tf) std::printf (" %5g@%-3g", f, l);
        std::printf ("\n");
        for (int m : metals)
        {
            std::printf ("  %-46s", kMetals[(size_t) m].menuName);
            for (double l : tl) for (double f : tf)
            {
                Channel630 ch; ch.prepare (bankFor (FS_HI), m);
                auto r = runSine (ch, FS_HI, f, l, 1.5, 0.5, 40.0);
                std::printf (" %9.5f", thdOf (r.out, f, FS_HI) * 100.0);
            }
            std::printf ("\n");
        }
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("F. CLIP POINT (owner ruling: core knee at +18 dBFS, 20 Hz)\n");
    {
        std::printf ("  %-12s", "dBFS");
        const std::vector<double> lv = { -12, -6, 0, 6, 12, 18, 24 };
        for (double l : lv) std::printf (" %10g", l);
        std::printf ("\n");
        for (int m : { 2, 7, 14 })
        {
            std::vector<double> th, pu;
            for (double l : lv)
            {
                Channel630 ch; ch.prepare (bankFor (FS_HI), m);
                auto r = runSine (ch, FS_HI, 20.0, l, 1.5, 1.0, 20.0);
                th.push_back (thdOf (r.out, 20.0, FS_HI) * 100); pu.push_back (r.peakU);
            }
            std::printf ("  %-12.12s", kMetals[(size_t) m].menuName);
            for (double t : th) std::printf (" %9.4f%%", t);
            std::printf ("\n  %-12s", "  peak u");
            for (double p : pu) std::printf (" %10.4f", p);
            std::printf ("\n");
            if (m == 2)
            {
                char b[96]; std::snprintf (b, sizeof b, "peak core u at +18 dBFS = %.4f (expect 1.00 +/-3%%)", pu[5]);
                check (std::abs (pu[5] - 1.0) < 0.03, "Knee placement", b);
                std::snprintf (b, sizeof b, "peak core u at 0 dBFS = %.4f (saturation term %.2e)", pu[2], 0.25 * std::pow (pu[2], 6.0));
                check (pu[2] < 0.13, "0 dBFS stays far below knee", b);
            }
        }
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("G. BROADBAND PROGRAMME LEVEL: pink noise 20 Hz-20 kHz (2nd-order), 6 s, fs 352.8 kHz\n");
    {
        const size_t N = (size_t) (8.0 * FS_HI), skip = (size_t) (2.0 * FS_HI);
        auto pn = pinkNoise (N, -18.0, 630);
        bandLimit (pn, FS_HI);
        double cuR = 0, worst = 0;
        std::vector<double> scOut;
        for (int m : metals)
        {
            Channel630 ch; ch.prepare (bankFor (FS_HI), m);
            std::vector<double> o (N);
            for (size_t i = 0; i < N; ++i) o[i] = ch.process (pn[i]);
            const double r = rmsOf (o, skip);
            if (m == 2) cuR = r;
            if (m == 0) scOut = o;
            if (m == 2 || cuR > 0) {}
            std::printf ("  %-46s %+.5f dB re input\n", kMetals[(size_t) m].menuName, dB (r / rmsOf (pn, skip)));
            if (cuR > 0) worst = std::max (worst, std::abs (dB (r / cuR)));
        }
        // recompute against copper for metals listed before copper
        {
            Channel630 c0; c0.prepare (bankFor (FS_HI), 0);
            std::vector<double> o (N);
            for (size_t i = 0; i < N; ++i) o[i] = c0.process (pn[i]);
            worst = std::max (worst, std::abs (dB (rmsOf (o, skip) / cuR)));
        }
        char b[96]; std::snprintf (b, sizeof b, "worst |metal - copper| = %.5f dB (limit 0.01)", worst);
        check (worst <= 0.01, "Programme level (R1)", b);

        if (! scOut.empty())
        {
            // superconductor vs ideal DC-free drive (no transformer at all)
            const double K = std::tan (design::kPi * design::kDcBlockHz / FS_HI);
            const double b0 = 1.0 / (1.0 + K), a1 = (1.0 - K) / (1.0 + K);
            double x1 = 0, y1 = 0, e = 0, s = 0;
            for (size_t i = 0; i < N; ++i)
            {
                const double y = b0 * (pn[i] - x1) + a1 * y1; x1 = pn[i]; y1 = y;
                if (i >= skip) { e += (scOut[i] - y) * (scOut[i] - y); s += y * y; }
            }
            char c[96]; std::snprintf (c, sizeof c, "residual %.1f dB re signal (limit -120)", 10 * std::log10 (e / s));
            check (10 * std::log10 (e / s) <= -120.0, "Superconductor = pristine environment null", c);
        }
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("H. SEAMLESS SWITCH: Copper -> Rene 41 at t = 1 s on pink noise (40 ms equal-gain fade)\n");
    {
        const double fs = FS_HI;
        const size_t N = (size_t) (4.0 * fs), sw = (size_t) fs, fade = (size_t) std::lround (0.040 * fs);
        auto pn = pinkNoise (N, -18.0, 41);
        bandLimit (pn, fs);
        Channel630 a; a.prepare (bankFor (fs), 2);
        Channel630 ref; ref.prepare (bankFor (fs), 14);
        std::vector<double> oa (N), oref (N);
        double maxStep = 0, maxStepRef = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (i == sw) a.selectMetal (14);
            oa[i] = a.process (pn[i]);
            oref[i] = ref.process (pn[i]);
            if (i > 0)
            {
                maxStepRef = std::max (maxStepRef, std::abs (oref[i] - oref[i - 1]));
                if (i >= sw - 10 && i <= sw + fade + 10) maxStep = std::max (maxStep, std::abs (oa[i] - oa[i - 1]));
            }
        }
        auto resid = [&] (size_t from, size_t to)
        {
            double e = 0, s = 0;
            for (size_t i = from; i < to; ++i) { e += (oa[i] - oref[i]) * (oa[i] - oref[i]); s += oref[i] * oref[i]; }
            return 10 * std::log10 (std::max (e, 1e-300) / s);
        };
        const double r1 = resid (sw + fade, sw + fade + (size_t) (0.25 * fs));
        const double r2 = resid (sw + fade + (size_t) (0.25 * fs), N);
        char b[128];
        std::snprintf (b, sizeof b, "after fade 0-250 ms: %.1f dB, 250 ms-3 s: %.1f dB re signal (limit -60 / -90)", r1, r2);
        check (r1 <= -60.0 && r2 <= -90.0, "Converges to a continuously running Rene 41", b);
        std::snprintf (b, sizeof b, "max sample step in switch window %.5f vs programme max %.5f", maxStep, maxStepRef);
        check (maxStep <= maxStepRef * 1.05, "No click", b);
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("I. ALIASING / HF PURITY at 44.1 kHz (Rene 41, 0 dBFS)\n");
    {
        for (double f : { 10000.0, 15000.0, 19000.0 })
        {
            Channel630 ch; ch.prepare (bankFor (FS_LO), 14);
            auto r = runSine (ch, FS_LO, f, 0.0, 2.0, 1.0, 40.0);
            const double t = thdnFit (r.out, f, FS_LO);
            char b[96]; std::snprintf (b, sizeof b, "%g Hz: THD+N (incl. aliases) %.2e %% (limit 1e-4 %%)", f, t * 100);
            check (t * 100 <= 1e-4, "HF purity", b);
        }
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("J. SILENCE, IMPULSE, LATENCY, STABILITY\n");
    {
        Channel630 ch; ch.prepare (bankFor (FS_LO), 14);
        double mx = 0; for (int i = 0; i < 100000; ++i) mx = std::max (mx, std::abs (ch.process (0.0)));
        check (mx == 0.0, "Digital silence in -> exact zero out", "");

        for (int m : { 0, 2, 14 })
        {
            Channel630 c; c.prepare (bankFor (FS_LO), m);
            std::vector<double> ir (1 << 16);
            for (size_t i = 0; i < ir.size(); ++i) ir[i] = c.process (i == 0 ? 1.0 : 0.0);
            size_t pk = 0; for (size_t i = 0; i < ir.size(); ++i) if (std::abs (ir[i]) > std::abs (ir[pk])) pk = i;
            // ringing detector (after the 64-sample skin-shelf response): 2nd difference of the tail (a smooth LF tail gives ~0; Nyquist ringing gives ~4x its amplitude)
            double ring = 0; for (size_t i = 64; i < 5000; ++i) ring = std::max (ring, std::abs (ir[i] - 2 * ir[i - 1] + ir[i - 2]));
            char b[160]; std::snprintf (b, sizeof b, "%s: peak at sample %zu, value %.6f, max 2nd-difference of tail n>=64 %.2e (limit 1e-9)",
                                        kMetals[(size_t) m].menuName, pk, ir[pk], ring);
            check (pk == 0 && std::isfinite (ir[pk]) && ring < 1e-9, "Zero latency, no ringing", b);
        }

        // 60 s of hot pink noise at 44.1 kHz through the most resistive metal
        Channel630 c; c.prepare (bankFor (FS_LO), 14);
        const auto pn = pinkNoise ((size_t) (60 * FS_LO), -6.0, 7);
        bool finite = true; int maxIt = 0;
        for (double x : pn) { const double y = c.process (x); finite &= std::isfinite (y); maxIt = std::max (maxIt, c.getActiveEngine().getLastIterations()); }
        char b[96]; std::snprintf (b, sizeof b, "max Newton iterations %d", maxIt);
        check (finite && maxIt < 24, "60 s hot programme stable", b);
    }
    std::printf ("\n");

    //--------------------------------------------------------------------------
    std::printf ("K. CPU (single core, this machine)\n");
    for (double fs : { FS_LO, FS_HI })
    {
        Channel630 ch; ch.prepare (bankFor (fs), 11);
        const auto pn = pinkNoise ((size_t) (5 * fs), -18.0, 3);
        const auto t0 = std::chrono::steady_clock::now();
        double sink = 0; for (double x : pn) sink += ch.process (x);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  fs %.1f kHz: %.1f ns/sample/channel, stereo uses %.1f %% of one core (x2 during a fade)%s\n",
                     fs / 1000.0, sec / (double) pn.size() * 1e9, 2.0 * sec / 5.0 * 100.0, sink == 12345.0 ? " " : "");
    }

    std::printf ("\nRESULT: %s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
