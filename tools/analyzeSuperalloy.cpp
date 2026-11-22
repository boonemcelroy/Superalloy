/*
  ==============================================================================
    Superalloy — analyzeSuperalloy.cpp
    Analyser suite run on the real plugin processor (the SuperalloyAudioProcessor
    a DAW loads), every metal, mono, double precision. Each metal is measured
    from a fresh prepareToPlay with that metal already selected, so no
    crossfade is ever inside a measurement.

    Analyses (each writes one CSV into <out-dir>):
      thd_freq.csv   harmonic distortion vs frequency at -18 and 0 dBFS
      thd_level.csv  fundamental gain + harmonics vs input level (20/50/100 Hz)
      imd.csv        SMPTE IMD (60 Hz + 7 kHz, 4:1) vs level
      step.csv       step response (-18 dBFS step)
      null.csv       programme null, pink noise at -16 dBFS RMS: 1/3-octave level of
                     (output - input) and (output - superconductor output), re input;
                     band_hz 0 = broadband 20 Hz - 20 kHz

    Tones: raised-cosine fade-in, settle, then a least-squares fit over an
    integer number of fundamental cycles of sin/cos at every harmonic plus a
    quadratic trend (absorbs the slow 0.01 Hz drive-blocker and magnetising
    transients).

    Usage: analyzeSuperalloy [--color] <out-dir> [sampleRate]   (default 352800)
  ==============================================================================
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/PluginProcessor.h"
#include "../Source/MetalTable.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    double gExtraSettleSeconds = 0.0;   // diagnostic: extra settle time before every tone fit

    //==========================================================================
    // Generic least squares through accumulated normal equations.
    struct LeastSquares
    {
        explicit LeastSquares (int m) : M (m), A ((size_t) (m * m), 0.0), b ((size_t) m, 0.0) {}

        void add (const double* basis, double y)
        {
            for (int r = 0; r < M; ++r)
            {
                const double br = basis[r];
                double* row = &A[(size_t) (r * M)];
                for (int c = r; c < M; ++c) row[c] += br * basis[c];
                b[(size_t) r] += br * y;
            }
        }

        std::vector<double> solve() const
        {
            std::vector<double> a = A, x = b;
            for (int r = 0; r < M; ++r)                // mirror upper triangle
                for (int c = 0; c < r; ++c) a[(size_t) (r * M + c)] = a[(size_t) (c * M + r)];
            for (int c = 0; c < M; ++c)
            {
                int p = c;
                for (int r = c + 1; r < M; ++r)
                    if (std::abs (a[(size_t) (r * M + c)]) > std::abs (a[(size_t) (p * M + c)])) p = r;
                if (p != c)
                {
                    for (int k = 0; k < M; ++k) std::swap (a[(size_t) (c * M + k)], a[(size_t) (p * M + k)]);
                    std::swap (x[(size_t) c], x[(size_t) p]);
                }
                const double piv = a[(size_t) (c * M + c)];
                for (int r = c + 1; r < M; ++r)
                {
                    const double k = a[(size_t) (r * M + c)] / piv;
                    if (k == 0.0) continue;
                    for (int k2 = c; k2 < M; ++k2) a[(size_t) (r * M + k2)] -= k * a[(size_t) (c * M + k2)];
                    x[(size_t) r] -= k * x[(size_t) c];
                }
            }
            for (int r = M - 1; r >= 0; --r)
            {
                double s = x[(size_t) r];
                for (int c = r + 1; c < M; ++c) s -= a[(size_t) (r * M + c)] * x[(size_t) c];
                x[(size_t) r] = s / a[(size_t) (r * M + r)];
            }
            return x;
        }

        int M;
        std::vector<double> A, b;
    };

    //==========================================================================
    // One mono processor per metal.
    struct Rig
    {
        Rig (int metal, double sampleRate, bool color = false) : fs (sampleRate)
        {
            auto* p = proc.apvts.getParameter ("metal");
            p->setValueNotifyingHost (p->convertTo0to1 ((float) metal));
            proc.apvts.getParameter ("contour")->setValueNotifyingHost (color ? 1.0f : 0.0f);
            juce::AudioProcessor::BusesLayout mono;
            mono.inputBuses.add (juce::AudioChannelSet::mono());
            mono.outputBuses.add (juce::AudioChannelSet::mono());
            proc.setBusesLayout (mono);
            proc.setPlayConfigDetails (1, 1, fs, kBlock);
            proc.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
            proc.prepareToPlay (fs, kBlock);
        }

        // Feeds gen(n) for n in [0, total), hands each output sample to sink(n, x, y).
        void run (int total, const std::function<double (int)>& gen,
                  const std::function<void (int, double, double)>& sink)
        {
            proc.reset();
            juce::AudioBuffer<double> buf (1, kBlock);
            std::vector<double> in ((size_t) kBlock);
            juce::MidiBuffer midi;
            for (int start = 0; start < total; start += kBlock)
            {
                const int n = std::min (kBlock, total - start);
                buf.setSize (1, n, false, false, true);
                double* d = buf.getWritePointer (0);
                for (int i = 0; i < n; ++i) { in[(size_t) i] = gen (start + i); d[i] = in[(size_t) i]; }
                proc.processBlock (buf, midi);
                for (int i = 0; i < n; ++i) sink (start + i, in[(size_t) i], d[i]);
            }
        }

        static constexpr int kBlock = 4096;
        SuperalloyAudioProcessor proc;
        double fs;
    };

    //==========================================================================
    struct ToneResult
    {
        double gainDB = 0.0;                 // fundamental out/in
        double phaseDeg = 0.0;
        std::array<double, 10> hDB {};       // index k: harmonic k re fundamental (dB); [0],[1] unused
        int nHarm = 1;
        double thdDB = -400.0;               // sqrt(sum H2..Hn) re fundamental
    };

    ToneResult measureTone (Rig& rig, double f, double amp, int maxHarm = 9)
    {
        const double fs = rig.fs;
        const double w = 2.0 * kPi * f / fs;
        int nH = 1;
        while (nH < maxHarm && (nH + 1) * f < 0.45 * fs) ++nH;

        const int ramp    = (int) std::lround (std::max (0.10 * fs, 6.0 * fs / f));
        const int settle  = ramp + (int) std::lround (std::max (0.25 * fs, 6.0 * fs / f) + gExtraSettleSeconds * fs);
        const int cycles  = (int) std::ceil (std::max (10.0, 0.25 * f));
        const int measure = (int) std::lround (cycles * fs / f);
        const int total   = settle + measure;

        const int M = 2 * nH + 3;
        LeastSquares ls (M);
        std::vector<double> basis ((size_t) M);
        const double tScale = 1.0 / (double) measure;

        rig.run (total,
            [&] (int n)
            {
                const double env = n < ramp ? 0.5 - 0.5 * std::cos (kPi * (double) n / (double) ramp) : 1.0;
                return amp * env * std::sin (w * (double) n);
            },
            [&] (int n, double, double y)
            {
                const int k = n - settle;
                if (k < 0) return;
                const double ph = w * (double) n;
                for (int h = 1; h <= nH; ++h)
                {
                    basis[(size_t) (2 * h - 2)] = std::sin (h * ph);
                    basis[(size_t) (2 * h - 1)] = std::cos (h * ph);
                }
                const double t = ((double) k + 0.5) * tScale - 0.5;
                basis[(size_t) (2 * nH)] = 1.0;
                basis[(size_t) (2 * nH + 1)] = t;
                basis[(size_t) (2 * nH + 2)] = t * t;
                ls.add (basis.data(), y);
            });

        const auto x = ls.solve();
        ToneResult r;
        r.nHarm = nH;
        const double a1 = std::hypot (x[0], x[1]);
        r.gainDB = 20.0 * std::log10 (a1 / amp);
        r.phaseDeg = std::atan2 (x[1], x[0]) * 180.0 / kPi;
        double sum = 0.0;
        for (int h = 2; h <= nH; ++h)
        {
            const double ah = std::hypot (x[(size_t) (2 * h - 2)], x[(size_t) (2 * h - 1)]);
            r.hDB[(size_t) h] = 20.0 * std::log10 (std::max (ah / a1, 1e-20));
            sum += ah * ah;
        }
        r.thdDB = 20.0 * std::log10 (std::max (std::sqrt (sum) / a1, 1e-20));
        return r;
    }

    //==========================================================================
    // SMPTE RP120 IMD: 60 Hz + 7 kHz at 4:1 amplitude; level = combined peak.
    double measureIMD (Rig& rig, double peakDBFS)
    {
        const double fs = rig.fs;
        const double peak = std::pow (10.0, peakDBFS / 20.0);
        const double aLo = peak * 0.8, aHi = peak * 0.2;
        const double fLo = 60.0, fHi = 7000.0;
        const int ramp = (int) std::lround (0.10 * fs);
        const int settle = ramp + (int) std::lround (0.40 * fs);
        const int measure = (int) std::lround (1.0 * fs);    // integer cycles of every frequency used
        const int total = settle + measure;

        // basis: 7 kHz and 7 kHz ± n·60 (n = 1..4) as sin/cos, plus 60 Hz and its first 3 harmonics, plus quadratic trend
        std::vector<double> freqs { fHi };
        for (int n = 1; n <= 4; ++n) { freqs.push_back (fHi - n * fLo); freqs.push_back (fHi + n * fLo); }
        for (int h = 1; h <= 3; ++h) freqs.push_back (h * fLo);
        const int M = 2 * (int) freqs.size() + 3;
        LeastSquares ls (M);
        std::vector<double> basis ((size_t) M);

        rig.run (total,
            [&] (int n)
            {
                const double env = n < ramp ? 0.5 - 0.5 * std::cos (kPi * (double) n / (double) ramp) : 1.0;
                const double t = (double) n / fs;
                return env * (aLo * std::sin (2.0 * kPi * fLo * t) + aHi * std::sin (2.0 * kPi * fHi * t));
            },
            [&] (int n, double, double y)
            {
                const int k = n - settle;
                if (k < 0) return;
                const double t = (double) n / fs;
                for (size_t i = 0; i < freqs.size(); ++i)
                {
                    basis[2 * i]     = std::sin (2.0 * kPi * freqs[i] * t);
                    basis[2 * i + 1] = std::cos (2.0 * kPi * freqs[i] * t);
                }
                const double tt = ((double) k + 0.5) / (double) measure - 0.5;
                basis[(size_t) (M - 3)] = 1.0; basis[(size_t) (M - 2)] = tt; basis[(size_t) (M - 1)] = tt * tt;
                ls.add (basis.data(), y);
            });

        const auto x = ls.solve();
        const double carrier = std::hypot (x[0], x[1]);
        double sum = 0.0;
        for (size_t i = 1; i <= 8; ++i)
        {
            const double a = std::hypot (x[2 * i], x[2 * i + 1]);
            sum += a * a;
        }
        return 20.0 * std::log10 (std::max (std::sqrt (sum) / carrier, 1e-20));
    }

    //==========================================================================
    // Exact pink noise at any sample rate: white Gaussian noise shaped by 1/sqrt(f)
    // in the frequency domain (1 Hz to Nyquist), normalised to the requested RMS.
    std::vector<double> pinkNoise (int n, double rmsDBFS, unsigned seed, double fs)
    {
        int order = 1; while ((1 << order) < n) ++order;
        const int n2 = 1 << order;
        std::mt19937_64 rng (seed);
        std::normal_distribution<double> nd (0.0, 1.0);
        std::vector<std::complex<double>> a ((size_t) n2);
        for (auto& x : a) x = { nd (rng), 0.0 };
        auto fft = [&] (bool inverse)
        {
            for (int i = 1, j = 0; i < n2; ++i)
            {
                int bit = n2 >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
            }
            for (int len = 2; len <= n2; len <<= 1)
            {
                const double ang = 2.0 * kPi / len * (inverse ? 1.0 : -1.0);
                const std::complex<double> wl (std::cos (ang), std::sin (ang));
                for (int i = 0; i < n2; i += len)
                {
                    std::complex<double> w (1.0, 0.0);
                    for (int k = 0; k < len / 2; ++k)
                    {
                        const auto u = a[(size_t) (i + k)], v = a[(size_t) (i + k + len / 2)] * w;
                        a[(size_t) (i + k)] = u + v; a[(size_t) (i + k + len / 2)] = u - v;
                        w *= wl;
                    }
                }
            }
            if (inverse) for (auto& x : a) x /= (double) n2;
        };
        fft (false);
        for (int k = 0; k <= n2 / 2; ++k)
        {
            const double f = (double) k * fs / n2;
            a[(size_t) k] *= f >= 1.0 ? 1.0 / std::sqrt (f) : 0.0;
            if (k > 0 && k < n2 / 2) a[(size_t) (n2 - k)] = std::conj (a[(size_t) k]);
        }
        a[(size_t) (n2 / 2)] = { a[(size_t) (n2 / 2)].real(), 0.0 };
        fft (true);
        std::vector<double> v ((size_t) n);
        double ss = 0;
        for (int i = 0; i < n; ++i) { v[(size_t) i] = a[(size_t) i].real(); ss += v[(size_t) i] * v[(size_t) i]; }
        const double g = std::pow (10.0, rmsDBFS / 20.0) / std::sqrt (ss / n);
        for (auto& x : v) x *= g;
        return v;
    }

    // 1/3-octave band powers via a Hann-windowed Welch PSD (exact bin sums).
    std::vector<double> thirdOctave (const std::vector<double>& x, double fs, const std::vector<double>& centres)
    {
        const int N = 1 << 17;                     // 2.7 Hz resolution at 352.8 kHz
        const int hop = N / 2;
        std::vector<double> psd ((size_t) (N / 2 + 1), 0.0);
        std::vector<double> win ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N);
        juce::dsp::FFT fft (17);
        std::vector<float> scratch ((size_t) (2 * N));
        int segs = 0;
        for (size_t start = 0; start + (size_t) N <= x.size(); start += (size_t) hop)
        {
            // juce::dsp::FFT is single precision: each segment is normalised to its own peak first,
            // so its rounding floor sits ~140 dB below that segment's own level.
            double peak = 1e-300;
            for (int i = 0; i < N; ++i) peak = std::max (peak, std::abs (x[start + (size_t) i]));
            for (int i = 0; i < N; ++i) scratch[(size_t) i] = (float) (x[start + (size_t) i] * win[(size_t) i] / peak);
            std::fill (scratch.begin() + N, scratch.end(), 0.0f);
            fft.performRealOnlyForwardTransform (scratch.data(), true);
            for (int k = 0; k <= N / 2; ++k)
            {
                const double re = scratch[(size_t) (2 * k)] * peak, im = scratch[(size_t) (2 * k + 1)] * peak;
                psd[(size_t) k] += re * re + im * im;
            }
            ++segs;
        }
        std::vector<double> bands;
        for (double fc : centres)
        {
            const double lo = fc / std::pow (2.0, 1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
            double s = 0;
            for (int k = 1; k <= N / 2; ++k)
            {
                const double f = (double) k * fs / N;
                if (f >= lo && f < hi) s += psd[(size_t) k];
            }
            bands.push_back (s / std::max (1, segs));
        }
        return bands;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    bool color = false;
    {
        int w = 1;
        for (int i = 1; i < argc; ++i)
            if (std::string (argv[i]) == "--color") color = true; else argv[w++] = argv[i];
        argc = w;
    }

    // Diagnostic: how the 20 Hz, 0 dBFS harmonics depend on settle time.
    if (argc > 1 && std::string (argv[1]) == "--settle-test")
    {
        for (int m : { 2, 11 })
        {
            Rig rig (m, 352800.0);
            for (double extra : { 0.0, 4.0, 16.0, 48.0 })
            {
                gExtraSettleSeconds = extra;
                const auto r = measureTone (rig, 20.0, 1.0);
                std::printf ("metal %2d  extra settle %5.1f s   H2 %8.2f  H3 %8.2f  H4 %8.2f  H5 %8.2f dB\n",
                             m, extra, r.hDB[2], r.hDB[3], r.hDB[4], r.hDB[5]);
                std::fflush (stdout);
            }
        }
        return 0;
    }

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const double fs = argc > 2 ? std::atof (argv[2]) : 352800.0;
    constexpr int NM = superalloy::kNumMetals;

    // grids
    std::vector<double> thdFreqs;
    for (double lf = 1.0; lf <= 4.0 + 1e-9; lf += 1.0 / 18.0) thdFreqs.push_back (std::pow (10.0, lf));   // 10 Hz – 10 kHz, 1/6 oct
    const std::vector<double> thdLevelsFreq { -18.0, 0.0 };
    std::vector<double> levels;
    for (double l = -40.0; l <= 36.0 + 1e-9; l += 2.0) levels.push_back (l);
    const std::vector<double> levelFreqs { 20.0, 50.0, 100.0 };
    std::vector<double> imdLevels;
    for (double l = -30.0; l <= 12.0 + 1e-9; l += 2.0) imdLevels.push_back (l);
    std::vector<double> centres;
    for (int k = -17; k <= 13; ++k) centres.push_back (1000.0 * std::pow (2.0, k / 3.0));       // 20 Hz – 20 kHz

    struct PerMetal
    {
        std::vector<std::vector<ToneResult>> thdF;   // [level][freq]
        std::vector<std::vector<ToneResult>> thdL;   // [freq][level]
        std::vector<double> imd;
        std::vector<std::pair<double, double>> step;
        std::vector<double> nullBands;      // (output - input)
        std::vector<double> nullScBands;    // (output - superconductor output)
    };
    std::vector<PerMetal> res ((size_t) NM);
    std::vector<double> inputBands;
    std::mutex lock;

    const int pinkLen = (int) std::lround (8.0 * fs);
    const auto pink = pinkNoise (pinkLen, -16.0, 630u, fs);
    {
        inputBands = thirdOctave (pink, fs, centres);
    }

    // superconductor (0 Ω) output on the same programme: the reference that
    // cancels everything the metals share (drive DC blocker, cores, makeup)
    std::vector<double> scOut ((size_t) pinkLen);
    {
        Rig sc (0, fs, color);
        sc.run (pinkLen, [&] (int n) { return pink[(size_t) n]; },
                [&] (int n, double, double y) { scOut[(size_t) n] = y; });
    }

    std::vector<std::thread> workers;
    for (int m = 0; m < NM; ++m)
    {
        workers.emplace_back ([&, m]
        {
            Rig rig (m, fs, color);
            auto& R = res[(size_t) m];

            for (double lv : thdLevelsFreq)
            {
                std::vector<ToneResult> row;
                for (double f : thdFreqs) row.push_back (measureTone (rig, f, std::pow (10.0, lv / 20.0)));
                R.thdF.push_back (row);
            }
            for (double f : levelFreqs)
            {
                std::vector<ToneResult> row;
                for (double lv : levels) row.push_back (measureTone (rig, f, std::pow (10.0, lv / 20.0)));
                R.thdL.push_back (row);
            }
            for (double lv : imdLevels) R.imd.push_back (measureIMD (rig, lv));

            // step: -18 dBFS step at t = 10 ms, record 0 – 600 ms
            {
                const double a = std::pow (10.0, -18.0 / 20.0);
                const int t0 = (int) std::lround (0.010 * fs);
                const int total = (int) std::lround (0.600 * fs);
                const int dec = std::max (1, (int) std::lround (fs / 20000.0));
                rig.run (total, [&] (int n) { return n >= t0 ? a : 0.0; },
                         [&] (int n, double, double y) { if (n % dec == 0) R.step.emplace_back ((double) n / fs, y / a); });
            }

            // programme null: (output - input) third-octave spectrum
            {
                std::vector<double> diff ((size_t) pinkLen), diffSc ((size_t) pinkLen);
                rig.run (pinkLen, [&] (int n) { return pink[(size_t) n]; },
                         [&] (int n, double x, double y) { diff[(size_t) n] = y - x; diffSc[(size_t) n] = y - scOut[(size_t) n]; });
                // discard the first second (DC blocker / magnetising start-up)
                std::vector<double> tail (diff.begin() + (long) fs, diff.end());
                R.nullBands = thirdOctave (tail, fs, centres);
                if (m == 0)
                    R.nullScBands.assign (centres.size(), 0.0);
                else
                {
                    std::vector<double> tailSc (diffSc.begin() + (long) fs, diffSc.end());
                    R.nullScBands = thirdOctave (tailSc, fs, centres);
                }
            }

            const std::lock_guard<std::mutex> g (lock);
            std::printf ("analysed %2d  %s\n", m, superalloy::kMetals[(size_t) m].menuName);
            std::fflush (stdout);
        });
    }
    for (auto& t : workers) t.join();

    // input bands over the same tail length (programme statistics are stationary)
    {
        std::vector<double> tail (pink.begin() + (long) fs, pink.end());
        inputBands = thirdOctave (tail, fs, centres);
    }

    auto open = [&] (const char* name) { return std::fopen (outDir.getChildFile (name).getFullPathName().toRawUTF8(), "w"); };

    if (FILE* f = open ("thd_freq.csv"))
    {
        std::fprintf (f, "metal,level_dbfs,freq_hz,gain_db,thd_db,h2_db,h3_db,h4_db,h5_db,h6_db,h7_db,h8_db,h9_db\n");
        for (int m = 0; m < NM; ++m)
            for (size_t li = 0; li < thdLevelsFreq.size(); ++li)
                for (size_t fi = 0; fi < thdFreqs.size(); ++fi)
                {
                    const auto& r = res[(size_t) m].thdF[li][fi];
                    std::fprintf (f, "%d,%.1f,%.6f,%.9f,%.4f", m, thdLevelsFreq[li], thdFreqs[fi], r.gainDB, r.thdDB);
                    for (int h = 2; h <= 9; ++h) std::fprintf (f, ",%.4f", h <= r.nHarm ? r.hDB[(size_t) h] : std::nan (""));
                    std::fprintf (f, "\n");
                }
        std::fclose (f);
    }
    if (FILE* f = open ("thd_level.csv"))
    {
        std::fprintf (f, "metal,freq_hz,level_dbfs,gain_db,thd_db,h2_db,h3_db,h4_db,h5_db,h6_db,h7_db,h8_db,h9_db\n");
        for (int m = 0; m < NM; ++m)
            for (size_t fi = 0; fi < levelFreqs.size(); ++fi)
                for (size_t li = 0; li < levels.size(); ++li)
                {
                    const auto& r = res[(size_t) m].thdL[fi][li];
                    std::fprintf (f, "%d,%.1f,%.1f,%.9f,%.4f", m, levelFreqs[fi], levels[li], r.gainDB, r.thdDB);
                    for (int h = 2; h <= 9; ++h) std::fprintf (f, ",%.4f", r.hDB[(size_t) h]);
                    std::fprintf (f, "\n");
                }
        std::fclose (f);
    }
    if (FILE* f = open ("imd.csv"))
    {
        std::fprintf (f, "metal,peak_dbfs,imd_db\n");
        for (int m = 0; m < NM; ++m)
            for (size_t li = 0; li < imdLevels.size(); ++li)
                std::fprintf (f, "%d,%.1f,%.4f\n", m, imdLevels[li], res[(size_t) m].imd[li]);
        std::fclose (f);
    }
    if (FILE* f = open ("step.csv"))
    {
        std::fprintf (f, "metal,t_s,y\n");
        for (int m = 0; m < NM; ++m)
            for (auto [t, y] : res[(size_t) m].step) std::fprintf (f, "%d,%.7f,%.10f\n", m, t, y);
        std::fclose (f);
    }
    if (FILE* f = open ("null.csv"))
    {
        auto dB = [] (double num, double den) { return 10.0 * std::log10 (std::max (num, 1e-300) / den); };
        std::fprintf (f, "metal,band_hz,null_vs_input_db,null_vs_superconductor_db\n");
        for (int m = 0; m < NM; ++m)
            for (size_t b = 0; b < centres.size(); ++b)
                std::fprintf (f, "%d,%.2f,%.4f,%.4f\n", m, centres[b],
                              dB (res[(size_t) m].nullBands[b], inputBands[b]),
                              dB (res[(size_t) m].nullScBands[b], inputBands[b]));
        double inTot = 0; for (double v : inputBands) inTot += v;
        for (int m = 0; m < NM; ++m)
        {
            double t = 0, ts = 0;
            for (double v : res[(size_t) m].nullBands) t += v;
            for (double v : res[(size_t) m].nullScBands) ts += v;
            std::fprintf (f, "%d,0,%.4f,%.4f\n", m, dB (t, inTot), dB (ts, inTot));   // band 0 = broadband 20 Hz – 20 kHz
        }
        std::fclose (f);
    }
    std::printf ("wrote analyses to %s\n", outDir.getFullPathName().toRawUTF8());
    return 0;
}
