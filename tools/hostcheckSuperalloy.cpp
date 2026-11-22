/*
  ==============================================================================
    Superalloy — hostcheckSuperalloy.cpp
    End-to-end check of the BUILT plugin binary, loaded the way a DAW loads it
    (JUCE's VST3 host), driven only through its exported parameters.

      1. The plugin loads and exports Metal, Contour and Bypass, and the host
         recognises Bypass as the plugin's bypass parameter (VST3 kIsBypass).
      2. Engaged, the plugin changes the signal; Metal and Contour reach the DSP.
      3. Bypass On: after the 40 ms crossfade the output equals the input bit
         for bit (float, and double where the host path supports it).
      4. Bypass Off: the output is identical to a second instance that was
         never bypassed (the engine kept running), and neither fade clicks.
      5. Saved state recalls Metal, Contour and Bypass in a new instance.

    Usage: hostcheckSuperalloy <path/to/Superalloy.vst3>
    Exit code 0 = all checks passed.
  ==============================================================================
*/
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ConsoleUtf8.h"

namespace
{
    int failures = 0;

    void check (bool ok, const char* what, const juce::String& detail)
    {
        std::printf ("  [%s] %s  %s\n", ok ? "PASS" : "FAIL", what, detail.toRawUTF8());
        if (! ok) ++failures;
    }

    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* q : p.getParameters())
            if (q->getName (64) == name)
                return q;
        return nullptr;
    }

    // Two independent channels of pink-ish noise (Kellet filter), -18 dBFS RMS.
    std::vector<std::vector<double>> makeNoise (int n, unsigned seed)
    {
        std::vector<std::vector<double>> out (2, std::vector<double> ((size_t) n));
        std::mt19937_64 rng (seed);
        std::normal_distribution<double> g (0.0, 1.0);
        for (auto& ch : out)
        {
            double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, ss = 0;
            for (auto& v : ch)
            {
                const double w = g (rng);
                b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
                b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
                b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
                v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
                ss += v * v;
            }
            const double k = std::pow (10.0, -18.0 / 20.0) / std::sqrt (ss / n);
            for (auto& v : ch) v *= k;
        }
        return out;
    }

    template <typename Sample>
    void runBlock (juce::AudioPluginInstance& p, const std::vector<std::vector<double>>& in,
                   std::vector<std::vector<double>>& out, int start, int n)
    {
        juce::AudioBuffer<Sample> buf (2, n);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                buf.setSample (c, i, (Sample) in[(size_t) c][(size_t) (start + i)]);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                out[(size_t) c][(size_t) (start + i)] = (double) buf.getSample (c, i);
    }

    double residualDB (const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b, int from, int to)
    {
        double e = 0, s = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = from; i < to; ++i)
            {
                const double d = a[(size_t) c][(size_t) i] - b[(size_t) c][(size_t) i];
                e += d * d; s += b[(size_t) c][(size_t) i] * b[(size_t) c][(size_t) i];
            }
        return 10.0 * std::log10 (std::max (e, 1e-300) / s);
    }

    double maxAbsDiff (const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b, int from, int to)
    {
        double m = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = from; i < to; ++i)
                m = std::max (m, std::abs (a[(size_t) c][(size_t) i] - b[(size_t) c][(size_t) i]));
        return m;
    }

    double maxStep (const std::vector<std::vector<double>>& a, int from, int to)
    {
        double m = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = std::max (1, from); i < to; ++i)
                m = std::max (m, std::abs (a[(size_t) c][(size_t) i] - a[(size_t) c][(size_t) (i - 1)]));
        return m;
    }

    std::unique_ptr<juce::AudioPluginInstance> load (juce::VST3PluginFormat& fmt, const juce::PluginDescription& d,
                                                     double fs, int block, bool dbl)
    {
        juce::String err;
        auto p = fmt.createInstanceFromDescription (d, fs, block, err);
        if (p == nullptr) { std::printf ("  load failed: %s\n", err.toRawUTF8()); return nullptr; }
        if (dbl) p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        p->prepareToPlay (fs, block);
        return p;
    }

    template <typename Sample>
    void runSuite (juce::VST3PluginFormat& fmt, const juce::PluginDescription& desc, double fs, int block)
    {
        const bool dbl = std::is_same_v<Sample, double>;
        std::printf ("\n%.1f kHz, block %d, %s precision\n", fs / 1000.0, block, dbl ? "double" : "single");

        auto a = load (fmt, desc, fs, block, dbl);    // the instance under test
        auto r = load (fmt, desc, fs, block, dbl);    // reference: never bypassed
        if (a == nullptr || r == nullptr) { check (false, "Plugin loads", "createInstanceFromDescription failed"); return; }
        if (dbl && ! a->isUsingDoublePrecision()) { std::printf ("  (host path is single precision only; skipped)\n"); return; }

        auto* metalA = findParam (*a, "Metal");   auto* metalR = findParam (*r, "Metal");
        auto* contA  = findParam (*a, "Contour"); auto* contR  = findParam (*r, "Contour");
        auto* bypA   = a->getBypassParameter();
        check (metalA != nullptr && contA != nullptr && findParam (*a, "Bypass") != nullptr,
               "Exports Metal, Contour, Bypass", juce::String (a->getParameters().size()) + " parameters");
        check (bypA != nullptr && bypA->getName (64) == "Bypass",
               "Host sees Bypass as the plugin's bypass (VST3 kIsBypass)", bypA != nullptr ? bypA->getName (64) : "none");
        if (metalA == nullptr || contA == nullptr || bypA == nullptr || metalR == nullptr || contR == nullptr) return;
        check (a->getLatencySamples() == 0, "Reports zero latency", juce::String (a->getLatencySamples()));

        const int n = (int) (4.0 * fs);
        auto in = makeNoise (n, 2026u);
        for (auto& ch : in)                 // the host buffer holds Sample: compare against exactly what went in
            for (auto& v : ch) v = (double) (Sample) v;
        std::vector<std::vector<double>> oa (2, std::vector<double> ((size_t) n)), orf = oa;
        const int fade = (int) std::lround (0.040 * fs);
        // Events happen at the start of the block containing the nominal time; every
        // check window below is anchored to that block start, not the nominal time.
        auto blockStart = [block] (double t) { return ((int) t / block) * block; };
        const int t1 = blockStart (fs), t2 = blockStart (2.0 * fs), t3 = blockStart (3.0 * fs), tState = blockStart (2.5 * fs);
        std::unique_ptr<juce::AudioPluginInstance> c;

        for (int start = 0; start < n; start += block)
        {
            const int len = std::min (block, n - start);
            if (start == t1)        // Metal -> René 41, Contour -> Color (both instances)
            {
                for (auto* p : { metalA, metalR }) p->setValueNotifyingHost (14.0f / 14.0f);
                for (auto* p : { contA, contR })   p->setValueNotifyingHost (1.0f);
            }
            if (start == t2) bypA->setValueNotifyingHost (1.0f);
            if (start == t3) bypA->setValueNotifyingHost (0.0f);
            if (start == tState)
            {
                juce::MemoryBlock state;
                a->getStateInformation (state);
                juce::String err;
                c = fmt.createInstanceFromDescription (desc, fs, block, err);
                if (c != nullptr) c->setStateInformation (state.getData(), (int) state.getSize());
            }
            runBlock<Sample> (*a, in, oa, start, len);
            runBlock<Sample> (*r, in, orf, start, len);
        }

        const double rDefault = residualDB (oa, in, (int) (0.5 * fs), t1);
        const double rColor   = residualDB (oa, in, (int) (1.5 * fs), t2);
        check (rDefault > -120.0, "Engaged: Copper / Subtle changes the signal", juce::String (rDefault, 1) + " dB residual vs input");
        check (rColor > -30.0 && rColor > rDefault + 10.0, "Metal + Contour reach the DSP (René 41 / Color)",
               juce::String (rColor, 1) + " dB residual vs input; Metal reads \"" + metalA->getCurrentValueAsText()
               + "\", Contour reads \"" + contA->getCurrentValueAsText() + "\"");

        const double byp = maxAbsDiff (oa, in, t2 + fade + block, t3);
        check (byp == 0.0, "Bypass On: output == input, bit for bit", "max |out - in| = " + juce::String (byp));

        const double re = maxAbsDiff (oa, orf, t3 + fade + block, n);
        check (re == 0.0, "Bypass Off: identical to an instance never bypassed", "max |diff| = " + juce::String (re));

        const double stepFade = std::max (maxStep (oa, t2 - block, t2 + fade + 2 * block), maxStep (oa, t3 - block, t3 + fade + 2 * block));
        const double stepRef  = std::max (maxStep (orf, 0, n), maxStep (in, 0, n));
        check (stepFade <= stepRef * 1.05, "No click entering or leaving bypass",
               "max step in fades " + juce::String (stepFade, 5) + " vs programme " + juce::String (stepRef, 5));

        if (c != nullptr)
        {
            auto* bc = c->getBypassParameter();
            auto* mc = findParam (*c, "Metal");
            auto* cc = findParam (*c, "Contour");
            const bool ok = bc != nullptr && mc != nullptr && cc != nullptr && bc->getValue() >= 0.5f
                         && mc->getCurrentValueAsText().contains ("41") && cc->getCurrentValueAsText() == "Color";
            check (ok, "Saved state recalls Metal, Contour, Bypass",
                   (mc ? mc->getCurrentValueAsText() : juce::String ("?")) + " / " + (cc ? cc->getCurrentValueAsText() : juce::String ("?"))
                   + " / Bypass " + (bc ? juce::String (bc->getValue(), 0) : juce::String ("?")));

            // the recalled instance is bypassed from its first sample
            if (dbl) c->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
            c->prepareToPlay (fs, block);
            std::vector<std::vector<double>> oc (2, std::vector<double> ((size_t) n));
            for (int start = 0; start < (int) fs; start += block)
                runBlock<Sample> (*c, in, oc, start, std::min (block, (int) fs - start));
            const double d = maxAbsDiff (oc, in, 0, (int) fs);
            check (d == 0.0, "Recalled instance is bypassed from its first sample", "max |out - in| = " + juce::String (d));
        }
        else
            check (false, "Saved state recalls Metal, Contour, Bypass", "could not create a third instance");
    }
}

int main (int argc, char** argv)
{
    superalloy::tools::useUtf8Console();
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: hostcheckSuperalloy <Superalloy.vst3>\n"); return 2; }
    const juce::String path = juce::File (juce::String (argv[1])).getFullPathName();

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile (types, path);
    std::printf ("Superalloy host check: %s\n", path.toRawUTF8());
    check (types.size() == 1, "VST3 found and described", types.isEmpty() ? juce::String ("no types")
                                                          : types[0]->name + " " + types[0]->version + " by " + types[0]->manufacturerName);
    if (types.isEmpty()) return 1;

    runSuite<float>  (fmt, *types[0], 48000.0, 480);
    runSuite<float>  (fmt, *types[0], 352800.0, 512);
    runSuite<double> (fmt, *types[0], 352800.0, 512);

    std::printf ("\nRESULT: %s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
