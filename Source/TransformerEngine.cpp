/*
  ==============================================================================
    Superalloy — TransformerEngine.cpp
  ==============================================================================
*/
#include "TransformerEngine.h"
#include <cmath>
#include <algorithm>

namespace superalloy
{
    using namespace design;

    void TransformerEngine::prepare (double sampleRate, int metalIndex) noexcept
    {
        T = 1.0 / sampleRate;
        setMetal (metalIndex);
        reset();
    }

    void TransformerEngine::setMetal (int metalIndex) noexcept
    {
        metal = std::clamp (metalIndex, 0, kNumMetals - 1);
        const auto w = windingFor (metal);
        G1 = 1.0 / w.Rp;                    // source → core 1   (T1 primary)
        G2 = 1.0 / (w.Rs + w.Rp);           // core 1 → core 2   (T1 secondary + T2 primary)
        G3 = 1.0 / (w.Rs + kRLoad);         // core 2 → load     (T2 secondary + RL)
        outRatio = kRLoad / (w.Rs + kRLoad);
    }

    void TransformerEngine::reset() noexcept
    {
        st = {};
        peakU = 0.0;
    }

    static_assert (kCoreP > 5.9999 && kCoreP < 6.0001, "coreEval computes |u|^6 directly");

    void TransformerEngine::coreEval (double phi, double& g, double& gp) noexcept
    {
        static const double phiS = fluxKnee();
        const double ur = coreUr;
        const double u  = phi / phiS;
        const double a  = std::abs (u);
        const double e  = std::exp (-a / ur);
        const double a2 = a * a, ap = a2 * a2 * a2;   // |u|^p, p = 6

        const double mag = kCoreR * a
                         + (1.0 - kCoreR) * ur * (-std::expm1 (-a / ur))
                         + kCoreR * ap * a / (kCoreP + 1.0);

        g  = (phiS / kLmInitial) * (u < 0.0 ? -mag : mag);
        gp = (kCoreR + (1.0 - kCoreR) * e + kCoreR * ap) / kLmInitial;
    }

    double TransformerEngine::processVolts (double vIn) noexcept
    {
        const double h = 0.5 * T;
        const double base1 = st.phi1 + h * st.x1;   // φ = base + h·x
        const double base2 = st.phi2 + h * st.x2;

        double x1 = st.x1, x2 = st.x2;   // Newton start: previous solution
        constexpr int kMaxIters = 30;
        constexpr double kTol = 1.0e-13; // volts (−262 dB re 0 dBFS)
        int it = 0;
        bool ok = false;

        for (; it < kMaxIters; ++it)
        {
            double g1, gp1, g2, gp2;
            coreEval (base1 + h * x1, g1, gp1);
            coreEval (base2 + h * x2, g2, gp2);

            // residuals (KCL, currents leaving the node)
            const double f1 = (x1 - vIn) * G1 + (x1 - x2) * G2 + g1;
            const double f2 = (x2 - x1) * G2 + x2 * G3 + g2;
            const double j11 = G1 + G2 + gp1 * h, j12 = -G2;
            const double j22 = G2 + G3 + gp2 * h;
            const double det = j11 * j22 - j12 * j12;
            const double d1 = ( j22 * f1 - j12 * f2) / det;
            const double d2 = (-j12 * f1 + j11 * f2) / det;
            x1 -= d1; x2 -= d2;
            if (std::max (std::abs (d1), std::abs (d2)) <= kTol) { ok = true; ++it; break; }
        }
        lastIters = it;

        if (! std::isfinite (x1) || ! std::isfinite (x2))
        {
            reset();       // safety: never emit non-finite audio
            return 0.0;
        }
        (void) ok;         // non-convergence within 30 iterations: keep best estimate

        st.phi1 = base1 + h * x1; st.x1 = x1;
        st.phi2 = base2 + h * x2; st.x2 = x2;

        static const double phiS = fluxKnee();
        peakU = std::max (peakU, std::max (std::abs (st.phi1), std::abs (st.phi2)) / phiS);

        return x2 * outRatio;
    }
}
