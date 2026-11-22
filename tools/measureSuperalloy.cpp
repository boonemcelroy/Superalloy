/*
  ==============================================================================
    Superalloy — measureSuperalloy.cpp
    Measures the frequency response of the real plugin processor (the same
    SuperalloyAudioProcessor a DAW loads), one metal at a time, the way an
    analyser does: stepped sines, least-squares fit of the fundamental.

    Method, per metal and per frequency:
      * fresh processor state (prepareToPlay with the metal already selected,
        so no crossfade is in the measurement)
      * sine of amplitude A at frequency f, faded in with a raised cosine (an
        abrupt start leaves a DC flux offset in the cores that biases the
        fundamental at high level), double-precision processBlock
      * discard the fade-in plus a settle window, then fit  y = a·sin + b·cos + c0 + c1·t + c2·t²
        over an integer number of cycles (the polynomial absorbs the slow
        0.01 Hz drive-blocker and magnetising-inductance transients)
      * gain = sqrt(a²+b²)/A, phase = atan2(b, a)

    Usage: measureSuperalloy <out.csv> [sampleRate] [levelDBFS ...]
    Defaults: 352800 Hz, levels -18 and 0 dBFS.
    CSV columns: metal,level_dbfs,freq_hz,mag_db,phase_deg
  ==============================================================================
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/MetalTable.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    struct Point { double f, magDB, phaseDeg; };

    // Solve the 5×5 normal equations (Gaussian elimination, partial pivoting).
    std::array<double, 5> solve5 (std::array<std::array<double, 6>, 5> m)
    {
        for (int c = 0; c < 5; ++c)
        {
            int p = c;
            for (int r = c + 1; r < 5; ++r)
                if (std::abs (m[(size_t) r][(size_t) c]) > std::abs (m[(size_t) p][(size_t) c])) p = r;
            std::swap (m[(size_t) c], m[(size_t) p]);
            for (int r = 0; r < 5; ++r)
            {
                if (r == c) continue;
                const double k = m[(size_t) r][(size_t) c] / m[(size_t) c][(size_t) c];
                for (int k2 = c; k2 < 6; ++k2) m[(size_t) r][(size_t) k2] -= k * m[(size_t) c][(size_t) k2];
            }
        }
        std::array<double, 5> x {};
        for (int r = 0; r < 5; ++r) x[(size_t) r] = m[(size_t) r][5] / m[(size_t) r][(size_t) r];
        return x;
    }

    Point measureOne (SuperalloyAudioProcessor& proc, double fs, double f, double amp)
    {
        proc.reset();
        const double w = 2.0 * kPi * f / fs;

        const int ramp    = (int) std::lround (std::max (0.10 * fs, 6.0 * fs / f));
        const int settle  = ramp + (int) std::lround (std::max (0.25 * fs, 6.0 * fs / f));
        const int cycles  = (int) std::ceil (std::max (10.0, 0.25 * f));
        const int measure = (int) std::lround (cycles * fs / f);
        const int total   = settle + measure;
        const int block   = 4096;

        juce::AudioBuffer<double> buf (2, block);
        juce::MidiBuffer midi;
        std::array<std::array<double, 6>, 5> N {};
        const double tScale = 1.0 / (double) measure;

        for (int start = 0; start < total; start += block)
        {
            const int n = std::min (block, total - start);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const int k = start + i;
                const double env = k < ramp ? 0.5 - 0.5 * std::cos (kPi * (double) k / (double) ramp) : 1.0;
                const double x = amp * env * std::sin (w * (double) k);
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);

            for (int i = 0; i < n; ++i)
            {
                const int k = start + i - settle;
                if (k < 0) continue;
                const double ph = w * (double) (start + i);
                const double t = ((double) k + 0.5) * tScale - 0.5;
                const std::array<double, 5> basis { std::sin (ph), std::cos (ph), 1.0, t, t * t };
                const double y = buf.getSample (0, i);
                for (size_t r = 0; r < 5; ++r)
                {
                    for (size_t c = 0; c < 5; ++c) N[r][c] += basis[r] * basis[c];
                    N[r][5] += basis[r] * y;
                }
            }
        }

        const auto x = solve5 (N);
        const double g = std::hypot (x[0], x[1]) / amp;
        return { f, 20.0 * std::log10 (g), std::atan2 (x[1], x[0]) * 180.0 / kPi };
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const std::string outPath = argc > 1 ? argv[1] : "superalloy_response.csv";
    const double fs = argc > 2 ? std::atof (argv[2]) : 352800.0;
    std::vector<double> levels;
    for (int i = 3; i < argc; ++i) levels.push_back (std::atof (argv[i]));
    if (levels.empty()) levels = { -18.0, 0.0 };

    // Log grid, 10 Hz to 0.45·fs, 48 points per decade.
    std::vector<double> freqs;
    const double fMax = 0.45 * fs;
    for (double lf = 1.0; lf <= std::log10 (fMax) + 1e-12; lf += 1.0 / 48.0)
        freqs.push_back (std::pow (10.0, lf));
    for (double extra : { 20.0, 1000.0, 20000.0 }) freqs.push_back (extra);
    std::sort (freqs.begin(), freqs.end());

    std::vector<std::vector<std::vector<Point>>> results (
        (size_t) superalloy::kNumMetals, std::vector<std::vector<Point>> (levels.size()));

    std::vector<std::thread> workers;
    std::mutex printLock;
    for (int m = 0; m < superalloy::kNumMetals; ++m)
    {
        workers.emplace_back ([&, m]
        {
            SuperalloyAudioProcessor proc;
            auto* p = proc.apvts.getParameter ("metal");
            p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
            proc.setPlayConfigDetails (2, 2, fs, 4096);
            proc.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
            proc.prepareToPlay (fs, 4096);

            for (size_t li = 0; li < levels.size(); ++li)
            {
                const double amp = std::pow (10.0, levels[li] / 20.0);
                for (double f : freqs)
                    results[(size_t) m][li].push_back (measureOne (proc, fs, f, amp));
            }
            const std::lock_guard<std::mutex> g (printLock);
            std::printf ("measured %2d  %s\n", m, superalloy::kMetals[(size_t) m].menuName);
        });
    }
    for (auto& t : workers) t.join();

    FILE* out = std::fopen (outPath.c_str(), "w");
    if (out == nullptr) { std::fprintf (stderr, "cannot write %s\n", outPath.c_str()); return 1; }
    std::fprintf (out, "metal,level_dbfs,freq_hz,mag_db,phase_deg\n");
    for (int m = 0; m < superalloy::kNumMetals; ++m)
        for (size_t li = 0; li < levels.size(); ++li)
            for (const auto& pt : results[(size_t) m][li])
                std::fprintf (out, "%d,%.1f,%.6f,%.9f,%.9f\n", m, levels[li], pt.f, pt.magDB, pt.phaseDeg);
    std::fclose (out);
    std::printf ("wrote %s (%zu frequencies, %zu levels, fs %.0f Hz)\n", outPath.c_str(), freqs.size(), levels.size(), fs);
    return 0;
}
