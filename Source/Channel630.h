/*
  ==============================================================================
    Superalloy — Channel630.h
    One audio channel:
        DC-free drive → transformer pair (selected metal) → skin shelf → makeup
        → CONTOUR (Subtle: nothing; Color: per-metal linear contour)

    Seamless metal changes. Every metal's two cores are kept magnetised in the
    background by a small-signal "shadow" model of that metal, so the incoming
    engine starts from its OWN consistent flux state (not the outgoing metal's).
    The two engines are then equal-gain crossfaded over 40 ms.
    Latency: zero samples.
  ==============================================================================
*/
#pragma once

#include "TransformerEngine.h"
#include "SkinShelf.h"
#include "ColorContour.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace superalloy
{
    //==========================================================================
    // Per-sample-rate design data for every metal (shared by all channels).
    struct MetalBank
    {
        std::array<SkinShelf, kNumMetals> shelf;
        std::array<double, kNumMetals> makeup {};
        // shadow model: inverse of the constant 2×2 small-signal matrix
        std::array<double, kNumMetals> i11 {}, i12 {}, i22 {}, g1 {};
        std::array<ColorDesign, kNumMetals> color {};
        double h = 0.0, fs = 0.0;

        // Magnitude of the Subtle chain for one metal (small signal).
        double subtleMagnitude (int m, double hz) const noexcept
        {
            return std::abs (design::networkTransfer (m, design::warpedHz (hz, fs)))
                 * shelf[(size_t) m].magnitudeAt (hz, fs) * makeup[(size_t) m];
        }

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            h = 0.5 / sampleRate;
            const double c = h / design::kLmInitial;
            for (int m = 0; m < kNumMetals; ++m)
            {
                const auto w = design::windingFor (m);
                const double G1 = 1.0 / w.Rp, G2 = 1.0 / (w.Rs + w.Rp), G3 = 1.0 / (w.Rs + design::kRLoad);
                const double a11 = G1 + G2 + c, a12 = -G2, a22 = G2 + G3 + c;
                const double det = a11 * a22 - a12 * a12;
                i11[(size_t) m] = a22 / det; i12[(size_t) m] = -a12 / det; i22[(size_t) m] = a11 / det;
                g1[(size_t) m] = G1;

                shelf[(size_t) m].design (m, fs);
                const double net = std::abs (design::networkTransfer (m, design::warpedHz (1000.0, fs)));
                makeup[(size_t) m] = 1.0 / (net * shelf[(size_t) m].magnitudeAt (1000.0, fs));
            }
            for (int m = 0; m < kNumMetals; ++m)
                color[(size_t) m].design (m, fs, [this, m] (double hz) { return subtleMagnitude (m, hz); });
        }
    };

    //==========================================================================
    class Channel630
    {
    public:
        static constexpr double kCrossfadeSeconds = 0.040;

        void prepare (const MetalBank& sharedBank, int metalIndex, bool colorOn = false) noexcept
        {
            bank = &sharedBank;
            const double fs = bank->fs;
            metalIndex = std::clamp (metalIndex, 0, kNumMetals - 1);
            for (int s = 0; s < 2; ++s)
            {
                eng[s].prepare (fs, metalIndex);
                shelf[s] = bank->shelf[(size_t) metalIndex];
                shelf[s].reset();
            }
            active = 0;
            fadeLen = std::max (1, (int) std::lround (kCrossfadeSeconds * fs));
            fadePos = -1;
            target = metalIndex;
            shadow.fill (CoreStates {});

            const double K = std::tan (design::kPi * design::kDcBlockHz / fs);
            hpB0 = 1.0 / (1.0 + K);
            hpA1 = (1.0 - K) / (1.0 + K);
            hpX1 = hpY1 = 0.0;

            color.prepare (bank->color, fs, colorOn);
        }

        void reset() noexcept
        {
            for (int s = 0; s < 2; ++s) { eng[s].reset(); shelf[s].reset(); }
            shadow.fill (CoreStates {});
            fadePos = -1;
            hpX1 = hpY1 = 0.0;
            color.reset();
        }

        // Safe to call at any time (the plugin calls it at block boundaries).
        void selectMetal (int metalIndex) noexcept
        {
            metalIndex = std::clamp (metalIndex, 0, kNumMetals - 1);
            if (metalIndex == target)
                return;

            if (fadePos >= 0)              // change during a fade: incoming engine becomes the source
            {
                active = 1 - active;
                fadePos = -1;
            }
            target = metalIndex;
            if (metalIndex == eng[active].getMetal())
                return;

            const int in = 1 - active;
            eng[in].setMetal (metalIndex);
            eng[in].setStates (shadow[(size_t) metalIndex]);   // its own magnetised state
            const auto& src = bank->shelf[(size_t) metalIndex];
            shelf[in] = src;
            shelf[in].copyStateFrom (shelf[active]);
            fadePos = 0;
        }

        // CONTOUR: false = Subtle, true = Color. Seamless (40 ms crossfade).
        void selectColor (bool on) noexcept  { color.setColor (on); }
        bool isColor() const noexcept        { return color.isColor(); }

        int  getTargetMetal() const noexcept { return target; }
        bool isFading() const noexcept       { return fadePos >= 0; }
        TransformerEngine& getActiveEngine() noexcept { return eng[active]; }

        // x: sample value (1.0 = 0 dBFS). Returns sample value.
        double process (double x) noexcept
        {
            int from, to;
            double w;
            const double y = processSubtle (x, from, to, w);
            return color.process (y, from, to, w);
        }

    private:
        double processSubtle (double x, int& from, int& to, double& w) noexcept
        {
            const double y = hpB0 * (x - hpX1) + hpA1 * hpY1;   // DC-free drive
            hpX1 = x; hpY1 = y;
            const double vIn = y * design::kVoltsAtFullScale;

            updateShadows (vIn);

            const double outA = runSlot (active, vIn);
            shadow[(size_t) eng[active].getMetal()] = eng[active].getStates(); // exact state of the running metal
            from = to = eng[active].getMetal();
            w = 1.0;
            if (fadePos < 0)
                return outA;

            const double outB = runSlot (1 - active, vIn);
            shadow[(size_t) eng[1 - active].getMetal()] = eng[1 - active].getStates();
            w = (double) (fadePos + 1) / (double) fadeLen;
            to = eng[1 - active].getMetal();
            const double out = outA + w * (outB - outA);
            if (++fadePos >= fadeLen)
            {
                active = 1 - active;
                fadePos = -1;
            }
            return out;
        }

        double runSlot (int s, double vIn) noexcept
        {
            const int m = eng[s].getMetal();
            const double v = eng[s].processVolts (vIn) * bank->makeup[(size_t) m] / design::kVoltsAtFullScale;
            return shelf[s].process (v);
        }

        void updateShadows (double vIn) noexcept
        {
            const double h = bank->h, invL = 1.0 / design::kLmInitial;
            for (int m = 0; m < kNumMetals; ++m)
            {
                auto& s = shadow[(size_t) m];
                const double b1 = s.phi1 + h * s.x1, b2 = s.phi2 + h * s.x2;
                const double r1 = vIn * bank->g1[(size_t) m] - b1 * invL;
                const double r2 = -b2 * invL;
                const double x1 = bank->i11[(size_t) m] * r1 + bank->i12[(size_t) m] * r2;
                const double x2 = bank->i12[(size_t) m] * r1 + bank->i22[(size_t) m] * r2;
                s.phi1 = b1 + h * x1; s.x1 = x1;
                s.phi2 = b2 + h * x2; s.x2 = x2;
            }
        }

        const MetalBank* bank = nullptr;
        TransformerEngine eng[2];
        SkinShelf shelf[2];
        std::array<CoreStates, kNumMetals> shadow {};
        int active = 0, target = kDefaultMetal;
        int fadeLen = 1, fadePos = -1;
        double hpB0 = 1.0, hpA1 = 1.0, hpX1 = 0.0, hpY1 = 0.0;
        ColorStage color;
    };
}
