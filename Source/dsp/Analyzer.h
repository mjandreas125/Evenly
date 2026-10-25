#pragma once
// Spectrum of the incoming audio (before processing) in third octaves, about 23 frames a second.
// The app uses these frames to tell speech from music and to correct the tone slowly.
#include "Loudness.h"
#include "Shared.h"
#include <complex>
#include <vector>

namespace rv
{
class Analyzer
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        n = fs > 60000 ? 8192 : 4096;
        hop = n / 2;
        win.resize ((size_t) n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2 * 3.141592653589793 * i / n);
        buf.assign ((size_t) n, 0.0f);
        fft.assign ((size_t) n, {});
        tw.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k) tw[(size_t) k] = std::polar (1.0f, (float) (-2 * 3.141592653589793 * k / n));
        // Bin ranges of each third octave.
        for (int b = 0; b < kBands; ++b)
        {
            const double lo = kBandHz[b] * std::pow (2.0, -1.0 / 6), hi = kBandHz[b] * std::pow (2.0, 1.0 / 6);
            binLo[b] = std::max (1, (int) std::ceil (lo * n / fs));
            binHi[b] = std::max (binLo[b], std::min (n / 2, (int) std::floor (hi * n / fs)));
        }
        kw[0].prepare (fs); kw[1].prepare (fs);
        reset();
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); fill = 0; kAcc = 0; kCount = 0; peak = 0; frameNo = 0; }

    // Feeds interleaved audio; calls emit(const AnalysisFrame&) for every finished frame.
    template <typename Emit>
    void process (const float* x, int frames, int channels, int stride, Emit&& emit)
    {
        for (int i = 0; i < frames; ++i)
        {
            const float* s = x + (size_t) i * (size_t) stride;
            const float l = s[0], r = channels > 1 ? s[1] : s[0];
            const double kl = kw[0].process (l), kr = kw[1].process (r);
            kAcc += kl * kl + kr * kr;
            ++kCount;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            buf[(size_t) fill++] = 0.5f * (l + r);
            if (fill == n)
            {
                emit (analyse());
                std::copy (buf.begin() + hop, buf.end(), buf.begin());
                fill = n - hop;
            }
        }
    }

private:
    AnalysisFrame analyse()
    {
        for (int i = 0; i < n; ++i) fft[(size_t) i] = { buf[(size_t) i] * win[(size_t) i], 0.0f };
        transform();
        AnalysisFrame f {};
        f.seq = ++frameNo;
        const double norm = 1.0 / ((double) n * n * 0.375 / 4);   // Hann power, one-sided
        for (int b = 0; b < kBands; ++b)
        {
            double p = 0;
            for (int k = binLo[b]; k <= binHi[b]; ++k) p += std::norm (fft[(size_t) k]);
            f.bandsDb[b] = (float) (10 * std::log10 (p * norm + 1e-20));
        }
        f.lufs = (float) powerToLufs (kCount ? kAcc / kCount : 0);
        f.peak = peak;
        kAcc = 0; kCount = 0; peak = 0;
        return f;
    }
    void transform()
    {
        const size_t N = (size_t) n;
        for (size_t i = 1, j = 0; i < N; ++i)
        {
            size_t bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (fft[i], fft[j]);
        }
        for (size_t len = 2; len <= N; len <<= 1)
        {
            const size_t step = N / len;
            for (size_t i = 0; i < N; i += len)
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = fft[i + k], v = fft[i + k + len / 2] * tw[k * step];
                    fft[i + k] = u + v;
                    fft[i + k + len / 2] = u - v;
                }
        }
    }

    double fs = 48000;
    int n = 4096, hop = 2048, fill = 0;
    std::vector<float> win, buf;
    std::vector<std::complex<float>> fft, tw;
    int binLo[kBands] {}, binHi[kBands] {};
    KWeight kw[2];
    double kAcc = 0;
    int kCount = 0;
    float peak = 0;
    uint32_t frameNo = 0;
};
} // namespace rv
