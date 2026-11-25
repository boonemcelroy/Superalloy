/*
  ==============================================================================
    Superalloy — fieldStudy.cpp
    Drive voltage vs magnetic field strength in the real transformer engine
    (TransformerEngine: the same code the plugin runs), from far below the
    630 V anchor to 60 kV on the virtual bus.

    Units
      V_bus   peak volts on the virtual 630 V bus = V_line · N,
              N = design::virtualTurnsRatio() = 4.566 (630 V at the knee).
      u       core flux ÷ knee flux (flux density B relative to B_knee).
      h       magnetising current ÷ magnetising current at the knee
              (field strength H relative to H_knee: H = N·i / l_e, so with the
              geometry fixed H is proportional to the magnetising current).
      µ_sec   secant permeability, (u/h) relative to its small-signal value.

    Method: cosine-started sine (no flux offset), settle, then measure over an
    integer number of cycles: peak u and h of the step-up core (T1) and the
    step-down core (T2), flux symmetry, output fundamental gain re small
    signal, output THD (H2–H9), Newton iterations, non-converged samples.

    Usage: fieldStudy <out-dir>     (writes sweep.csv, knee.csv, waves.csv, law.csv)
  ==============================================================================
*/
#include "TransformerEngine.h"
#include "ConsoleUtf8.h"

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace superalloy;
using cd = std::complex<double>;

namespace
{
    constexpr double kFs = 352800.0;
    const double kPi = design::kPi;

    double hOf (double phi)                     // magnetising current at flux phi
    {
        double g, gp; TransformerEngine::coreEval (phi, g, gp); return g;
    }
    double iKnee() { return hOf (design::fluxKnee()); }

    cd bin (const std::vector<double>& x, double f)      // Blackman-Harris windowed DFT bin
    {
        const size_t N = x.size(); cd acc = 0.0; double ws = 0.0;
        for (size_t n = 0; n < N; ++n)
        {
            const double t = 2 * kPi * (double) n / (double) (N - 1);
            const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
            acc += w * x[n] * std::polar (1.0, -2 * kPi * f * (double) n / kFs); ws += w;
        }
        return 2.0 * acc / ws;
    }

    struct Point
    {
        int metal = 0; double f = 0, vbus = 0;
        double u1 = 0, u2 = 0, h1 = 0, h2 = 0, asym = 0, gainDB = 0, thdDB = -999, maxIt = 0, nonConv = 0;
        bool finite = true;
    };

    Point measure (int metal, double f, double vbus, std::vector<double>* wave = nullptr)
    {
        TransformerEngine e; e.prepare (kFs, metal);
        const double N = design::virtualTurnsRatio();
        const double A = vbus / N;                             // line-referred peak volts
        const double settle = std::max (1.0, 20.0 / f), meas = std::max (0.5, 40.0 / f);
        const size_t nS = (size_t) (settle * kFs), nM = (size_t) (meas * kFs);
        std::vector<double> in, out; in.reserve (nM); out.reserve (nM);
        Point p; p.metal = metal; p.f = f; p.vbus = vbus;
        double pmax = -1e300, pmin = 1e300;
        const double phiS = design::fluxKnee(), ik = iKnee();
        const size_t cyc = (size_t) std::llround (kFs / f);
        for (size_t n = 0; n < nS + nM; ++n)
        {
            const double x = A * std::cos (2 * kPi * f * (double) n / kFs);
            const double y = e.processVolts (x);
            if (! std::isfinite (y)) p.finite = false;
            if (n >= nS)
            {
                const auto& s = e.getStates();
                p.u1 = std::max (p.u1, std::abs (s.phi1) / phiS);
                p.u2 = std::max (p.u2, std::abs (s.phi2) / phiS);
                p.h1 = std::max (p.h1, std::abs (hOf (s.phi1)) / ik);
                p.h2 = std::max (p.h2, std::abs (hOf (s.phi2)) / ik);
                pmax = std::max (pmax, s.phi1); pmin = std::min (pmin, s.phi1);
                p.maxIt = std::max (p.maxIt, (double) e.getLastIterations());
                if (e.getLastIterations() >= 30) p.nonConv += 1;
                in.push_back (x); out.push_back (y);
                if (wave != nullptr && n >= nS + nM - cyc)
                {
                    wave->push_back (s.phi1 / phiS);
                    wave->push_back (hOf (s.phi1) / ik);
                    wave->push_back (x * N);                       // bus volts
                    wave->push_back (y / (A > 0 ? A : 1.0));      // output re input peak
                }
            }
        }
        p.asym = (pmax + pmin) / std::max (pmax - pmin, 1e-300);
        const double f1 = std::abs (bin (out, f)), fi = std::abs (bin (in, f));
        // small-signal reference gain of the same metal (linear network at this frequency)
        const auto wnd = design::windingFor (metal);
        const double ref = std::abs (design::networkTransfer (metal, design::warpedHz (f, kFs))) * 1.0;
        p.gainDB = 20 * std::log10 (std::max (f1 / fi, 1e-300) / ref);
        double h2 = 0;
        for (int k = 2; k <= 9 && k * f < 0.45 * kFs; ++k) { const double a = std::abs (bin (out, k * f)); h2 += a * a; }
        p.thdDB = 10 * std::log10 (std::max (h2, 1e-300) / (f1 * f1));
        (void) wnd;
        return p;
    }
}

int main (int argc, char** argv)
{
    superalloy::tools::useUtf8Console();
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int metals[] = { 0, 2, 7, 14 };     // superconductor, copper, Monel 400, René 41
    std::printf ("N = %.4f, knee flux %.6f V*s, knee magnetising current %.4f mA (line-referred), V_line at knee %.3f Vpk\n",
                 design::virtualTurnsRatio(), design::fluxKnee(), iKnee() * 1e3, 630.0 / design::virtualTurnsRatio());

    // 1. the static law of the modelled core: u -> h, secant and incremental permeability
    {
        std::FILE* f = std::fopen ((dir + "/law.csv").c_str(), "w");
        std::fprintf (f, "u,h,mu_sec_rel,mu_inc_rel\n");
        const double phiS = design::fluxKnee(), ik = iKnee();
        double g0, gp0; TransformerEngine::coreEval (1e-9 * phiS, g0, gp0);
        const double sec0 = (1e-9 * phiS) / g0, inc0 = 1.0 / gp0;
        for (int i = 0; i <= 600; ++i)
        {
            const double u = std::pow (10.0, -3.0 + 4.0 * i / 600.0);     // 0.001 .. 10
            double g, gp; TransformerEngine::coreEval (u * phiS, g, gp);
            std::fprintf (f, "%.8g,%.8g,%.8g,%.8g\n", u, g / ik, (u * phiS / g) / sec0, (1.0 / gp) / inc0);
        }
        std::fclose (f);
    }

    std::mutex lock;
    auto parallel = [] (int n, auto fn)
    {
        std::atomic<int> next { 0 };
        std::vector<std::thread> pool;
        const int t = (int) std::max (1u, std::thread::hardware_concurrency());
        for (int k = 0; k < t; ++k) pool.emplace_back ([&] { for (int j = next++; j < n; j = next++) fn (j); });
        for (auto& th : pool) th.join();
    };

    // 2. voltage sweep at 20, 100, 1000 Hz: 0.63 V .. 60 kV on the bus, 8 points per decade
    {
        std::vector<double> volts;
        for (double lv = std::log10 (0.63); lv <= std::log10 (60000.0) + 1e-9; lv += 1.0 / 8.0) volts.push_back (std::pow (10.0, lv));
        for (double v : { 63.0, 79.3, 630.0, 6300.0, 60000.0 }) volts.push_back (v);
        std::vector<Point> pts;
        for (double fq : { 20.0, 100.0, 1000.0 })
            for (int m : metals)
                for (double v : volts) { Point p; p.metal = m; p.f = fq; p.vbus = v; pts.push_back (p); }
        parallel ((int) pts.size(), [&] (int j) { pts[(size_t) j] = measure (pts[(size_t) j].metal, pts[(size_t) j].f, pts[(size_t) j].vbus); });
        std::FILE* f = std::fopen ((dir + "/sweep.csv").c_str(), "w");
        std::fprintf (f, "metal,freq_hz,vbus,u1,u2,h1,h2,flux_asym,gain_db,thd_db,max_iters,nonconverged,finite\n");
        for (const auto& p : pts)
            std::fprintf (f, "%d,%g,%.6g,%.8g,%.8g,%.8g,%.8g,%.3g,%.6f,%.3f,%g,%g,%d\n", p.metal, p.f, p.vbus, p.u1, p.u2, p.h1, p.h2,
                          p.asym, p.gainDB, p.thdDB, p.maxIt, p.nonConv, p.finite ? 1 : 0);
        std::fclose (f);
    }

    // 3. knee voltage vs frequency: V_bus at which the step-up core reaches u = 1 (bisection, <= 60 kV)
    {
        const double freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        struct K { int m; double f, vk, u60; };
        std::vector<K> ks;
        for (int m : { 2, 14 }) for (double fq : freqs) ks.push_back ({ m, fq, -1, 0 });
        parallel ((int) ks.size(), [&] (int j)
        {
            auto& k = ks[(size_t) j];
            k.u60 = measure (k.m, k.f, 60000.0).u1;
            if (k.u60 < 1.0) return;
            double lo = 1.0, hi = 60000.0;
            for (int it = 0; it < 26; ++it)
            {
                const double mid = std::sqrt (lo * hi);
                (measure (k.m, k.f, mid).u1 < 1.0 ? lo : hi) = mid;
            }
            k.vk = std::sqrt (lo * hi);
        });
        std::FILE* f = std::fopen ((dir + "/knee.csv").c_str(), "w");
        std::fprintf (f, "metal,freq_hz,vknee_bus,u_at_60kV\n");
        for (const auto& k : ks) std::fprintf (f, "%d,%g,%.6g,%.6g\n", k.m, k.f, k.vk, k.u60);
        std::fclose (f);
    }

    // 4. one cycle of flux, field and output at 63 V, 630 V, 6.3 kV, 60 kV (copper and René 41, 20 Hz)
    {
        std::FILE* f = std::fopen ((dir + "/waves.csv").c_str(), "w");
        std::fprintf (f, "metal,vbus_level,k,u,h,vbus,out_rel\n");
        for (int m : { 2, 14 })
            for (double v : { 63.0, 630.0, 6300.0, 60000.0 })
            {
                std::vector<double> w;
                measure (m, 20.0, v, &w);
                for (size_t k = 0; k + 3 < w.size(); k += 4)
                    std::fprintf (f, "%d,%g,%zu,%.8g,%.8g,%.8g,%.8g\n", m, v, k / 4, w[k], w[k + 1], w[k + 2], w[k + 3]);
            }
        std::fclose (f);
    }
    std::printf ("wrote %s/{law,sweep,knee,waves}.csv\n", dir.c_str());
    return 0;
}
