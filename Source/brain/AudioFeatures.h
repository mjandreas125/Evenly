#pragma once
// Speech-or-music evidence from the engine's analysis frames (third octaves, ~23 per second),
// over a 4 s window:
//   pauses   - share of frames far quieter than the window's typical level (speech has many)
//   syllable - share of the voice-band level fluctuation at syllable rate, 2.5..7 Hz
//   bass     - steady energy below 120 Hz relative to the voice band (music)
//   air      - steady energy above 8 kHz relative to the voice band (cymbals, music)
//   flux     - how much the spectral shape changes from frame to frame (speech)
// The weights that turn them into P(speech) are fitted on reference clips
// (tools/train_classifier.py -> ClassifierWeights.inc).
#include "../dsp/Shared.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <deque>
#include <vector>

namespace rv
{
struct Features { float pauses, syllable, bass, air, flux; };

class FeatureWindow
{
public:
    static constexpr int kLen = 94;   // ~4 s

    void reset() { frames.clear(); }
    bool ready() const { return (int) frames.size() >= kLen; }

    // Returns false for frames that carry no sound (they are not added).
    bool push (const AnalysisFrame& f)
    {
        if (f.lufs < -65) return false;
        Frame x {};
        double voice = 0, low = 0, high = 0;
        for (int b = 0; b < kBands; ++b)
        {
            const double p = std::pow (10.0, f.bandsDb[b] / 10.0);
            x.db[b] = f.bandsDb[b];
            if (kBandHz[b] >= 300 && kBandHz[b] <= 3400) voice += p;
            else if (kBandHz[b] < 120) low += p;
            else if (kBandHz[b] >= 8000) high += p;
        }
        x.voiceDb = (float) (10 * std::log10 (voice + 1e-20));
        x.lowDb = (float) (10 * std::log10 (low + 1e-20));
        x.highDb = (float) (10 * std::log10 (high + 1e-20));
        frames.push_back (x);
        if ((int) frames.size() > kLen) frames.pop_front();
        return true;
    }

    Features compute() const
    {
        Features out {};
        const int n = (int) frames.size();
        if (n < 8) return out;
        // Typical level = median of the voice band; pauses = frames 10 dB or more below it.
        std::vector<float> v;
        for (const auto& f : frames) v.push_back (f.voiceDb);
        std::vector<float> s = v;
        std::nth_element (s.begin(), s.begin() + n / 2, s.end());
        const float med = s[(size_t) n / 2];
        int quiet = 0;
        for (float x : v) if (x < med - 10) ++quiet;
        out.pauses = (float) quiet / n;
        // Syllable-rate share of the envelope fluctuation (DFT of the mean-removed envelope).
        double mean = 0;
        for (float x : v) mean += std::max (x, med - 30);
        mean /= n;
        double total = 0, syl = 0;
        const double fps = 48000.0 / 2048.0;
        for (int k = 1; k < n / 2; ++k)
        {
            std::complex<double> acc = 0;
            for (int i = 0; i < n; ++i) acc += (std::max (v[(size_t) i], med - 30) - mean) * std::polar (1.0, -2 * 3.141592653589793 * k * i / n);
            const double p = std::norm (acc), hz = k * fps / n;
            total += p;
            if (hz >= 2.5 && hz <= 7.0) syl += p;
        }
        out.syllable = total > 0 ? (float) (syl / total) : 0;
        // Bass and air: median over the window, relative to the voice band (pauses excluded).
        std::vector<float> lo, hi;
        for (const auto& f : frames)
            if (f.voiceDb >= med - 10) { lo.push_back (f.lowDb - f.voiceDb); hi.push_back (f.highDb - f.voiceDb); }
        auto median = [] (std::vector<float> a) { if (a.empty()) return -40.0f; std::nth_element (a.begin(), a.begin() + a.size() / 2, a.end()); return a[a.size() / 2]; };
        out.bass = median (lo);
        out.air = median (hi);
        // Spectral flux of the shape (level removed), voice frames only, in dB.
        double flux = 0;
        int m = 0;
        for (int i = 1; i < n; ++i)
        {
            if (frames[(size_t) i].voiceDb < med - 10 || frames[(size_t) i - 1].voiceDb < med - 10) continue;
            double d = 0;
            for (int b = 4; b < 22; ++b)
            {
                const double a1 = frames[(size_t) i].db[b] - frames[(size_t) i].voiceDb, a0 = frames[(size_t) i - 1].db[b] - frames[(size_t) i - 1].voiceDb;
                d += std::abs (a1 - a0);
            }
            flux += d / 18;
            ++m;
        }
        out.flux = m ? (float) (flux / m) : 0;
        return out;
    }

private:
    struct Frame { float db[kBands]; float voiceDb, lowDb, highDb; };
    std::deque<Frame> frames;
};
} // namespace rv
