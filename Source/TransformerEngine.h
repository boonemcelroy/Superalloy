/*
  ==============================================================================
    Superalloy — TransformerEngine.h
    Sample-by-sample simulation of the step-up / step-down transformer pair
    wound in one metal:

        v_in ─ Rp ─┬─ (Rs + Rp) ─┬─ Rs ─┬─ v_out
                  Lm1           Lm2    RL
                 (core T1)     (core T2)

    Trapezoidal integration of both nonlinear cores (A-stable, exact bilinear
    mapping of the small-signal network), 2×2 Newton per sample. No element
    has a time constant below the sample period, so there are no stiff modes.
  ==============================================================================
*/
#pragma once

#include "TransformerDesign.h"

namespace superalloy
{
    struct CoreStates
    {
        double phi1 = 0.0, phi2 = 0.0;   // core fluxes (V·s, line-referred)
        double x1 = 0.0, x2 = 0.0;       // node voltages at the cores, previous sample
    };

    class TransformerEngine
    {
    public:
        void prepare (double sampleRate, int metalIndex) noexcept;
        void setMetal (int metalIndex) noexcept;   // keeps physical state
        void reset() noexcept;

        const CoreStates& getStates() const noexcept   { return st; }
        void setStates (const CoreStates& s) noexcept  { st = s; }

        // Volts in → volts out (load voltage). Makeup gain NOT applied.
        double processVolts (double vIn) noexcept;

        int    getMetal() const noexcept          { return metal; }
        double getPeakCoreU() const noexcept      { return peakU; }
        void   clearPeakCoreU() noexcept          { peakU = 0.0; }
        int    getLastIterations() const noexcept { return lastIters; }

        // Core magnetisation i = g(φ) and dg/dφ (shared with the harness).
        static void coreEval (double phi, double& g, double& gp) noexcept;

        // Core shape constant. Production value = design::kCoreUrCalibrated.
        // Only tools/verifySuperalloy --calibrate changes it (single-threaded).
        static inline double coreUr = design::kCoreUrCalibrated;

    private:
        double T = 1.0 / 48000.0;
        int metal = kDefaultMetal;
        double G1 = 0.0, G2 = 0.0, G3 = 0.0, outRatio = 1.0;
        CoreStates st;
        double peakU = 0.0;
        int lastIters = 0;
    };
}
