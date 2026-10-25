#pragma once
// Loudness the way people hear it (ITU-R BS.1770 K-weighting), a leveler that rides it toward a
// target, and a look-ahead peak limiter. Real-time safe: no allocation after prepare().
#include <algorithm>
#include <cmath>
#include <vector>

namespace rv
{
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double powerToLufs (double z) { return -0.691 + 10.0 * std::log10 (z + 1e-20); }

// BS.1770 pre-filter (high shelf) + RLB high-pass for any sample rate (pyloudnorm form).
class KWeight
{
public:
    void prepare (double fs)
    {
        {
            const double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
            const double A = std::pow (10.0, G / 40), w0 = 2 * 3.141592653589793 * fc / fs, al = std::sin (w0) / (2 * Q), c = std::cos (w0);
            const double a0 = (A + 1) - (A - 1) * c + 2 * std::sqrt (A) * al;
            s1 = { A * ((A + 1) + (A - 1) * c + 2 * std::sqrt (A) * al) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0,
                   A * ((A + 1) + (A - 1) * c - 2 * std::sqrt (A) * al) / a0, 2 * ((A - 1) - (A + 1) * c) / a0,
                   ((A + 1) - (A - 1) * c - 2 * std::sqrt (A) * al) / a0 };
        }
        {
            const double Q = 0.5003270373238773, fc = 38.13547087602444;
            const double w0 = 2 * 3.141592653589793 * fc / fs, al = std::sin (w0) / (2 * Q), c = std::cos (w0), a0 = 1 + al;
            s2 = { (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
        }
        reset();
    }
    void reset() { z1a = z2a = z1b = z2b = 0; }
    double process (double x)
    {
        const double y1 = s1.b0 * x + z1a;
        z1a = s1.b1 * x - s1.a1 * y1 + z2a;
        z2a = s1.b2 * x - s1.a2 * y1;
        const double y2 = s2.b0 * y1 + z1b;
        z1b = s2.b1 * y1 - s2.a1 * y2 + z2b;
        z2b = s2.b2 * y1 - s2.a2 * y2;
        return y2;
    }
private:
    struct Bq { double b0, b1, b2, a1, a2; } s1 {}, s2 {};
    double z1a = 0, z2a = 0, z1b = 0, z2b = 0;
};

// Momentary (400 ms) and short-term (3 s) loudness from 100 ms blocks.
class LoudnessMeter
{
public:
    void prepare (double fs)
    {
        for (auto& k : kw) k.prepare (fs);
        blockLen = std::max (1, (int) std::lround (fs * 0.1));
        reset();
    }
    void reset()
    {
        for (auto& k : kw) k.reset();
        acc = 0; count = 0; head = 0; filled = 0;
        std::fill (std::begin (blocks), std::end (blocks), 0.0);
        momentary = shortTerm = -70;
    }
    // Interleaved; channels beyond the first two count like BS.1770 front channels (weight 1).
    void process (const float* x, int n, int channels, int stride)
    {
        for (int i = 0; i < n; ++i)
        {
            const float* s = x + (size_t) i * (size_t) stride;
            for (int c = 0; c < std::min (channels, 2); ++c)
            {
                const double y = kw[c].process (s[c]);
                acc += y * y;
            }
            if (++count >= blockLen)
            {
                blocks[head] = acc / count;
                head = (head + 1) % 30;
                filled = std::min (filled + 1, 30);
                acc = 0; count = 0;
                momentary = powerToLufs (mean (4));
                shortTerm = powerToLufs (mean (30));
            }
        }
    }
    double momentaryLufs() const { return momentary; }
    double shortTermLufs() const { return shortTerm; }
private:
    double mean (int k) const
    {
        k = std::min (k, filled);
        if (k == 0) return 0;
        double s = 0;
        for (int i = 1; i <= k; ++i) s += blocks[(head - i + 30) % 30];
        return s / k;
    }
    KWeight kw[2];
    int blockLen = 4800, count = 0, head = 0, filled = 0;
    double acc = 0, blocks[30] {}, momentary = -70, shortTerm = -70;
};

struct LevelerSettings
{
    double targetLufs = -16, maxBoostDb = 12, maxCutDb = 18;
    double riseDbPerSec = 2.0;   // quiet passages come up this fast at most
    double jumpGuardDb = 3.0;    // a sudden jump may sit at most this much over the target (music needs room)
    double quietToNewSec = 2.0;  // a quiet stretch this long is new (quieter) content, not a pause
};

// Rides the gain so that the loudness (K-weighted, ignoring pauses) sits at the target.
// Quiet passages come up slowly; a sudden jump (an advert, an explosion) is pulled down fast.
class Leveler
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& k : kw) k.prepare (fs);
        reset();
    }
    void reset()
    {
        for (auto& k : kw) k.reset();
        pSlow = pFast = 0; haveEstimate = false; silence = 0; quietRun = 0; activeTime = 0;
        gainDb = 0; appliedGain = 1;
    }
    double gainDb_() const { return gainDb; }
    double slowLufs() const { return haveEstimate ? powerToLufs (pSlow) : -70; }

    // Measures the block, updates the gain and applies it (ramped) to all `channels`.
    void process (float* x, int n, int channels, int stride, const LevelerSettings& s)
    {
        if (n <= 0) return;
        double z = 0;
        for (int i = 0; i < n; ++i)
        {
            const float* p = x + (size_t) i * (size_t) stride;
            for (int c = 0; c < std::min (channels, 2); ++c)
            {
                const double y = kw[c].process (p[c]);
                z += y * y;
            }
        }
        z /= n;
        const double dur = n / fs, lk = powerToLufs (z);
        // A block far below the current level is a pause in speech or music: it must not pull the
        // estimate down (or the gain up). But if "pauses" go on for 2 s, it is quieter content.
        const bool audible = lk > -60;
        const bool belowLevel = haveEstimate && lk <= powerToLufs (pSlow) - 18;
        quietRun = audible && belowLevel ? quietRun + dur : 0;
        const bool newQuieter = quietRun > s.quietToNewSec;
        const bool active = audible && (! belowLevel || newQuieter);
        if (active)
        {
            // Never come back from a pause with a big boost: the next thing may be loud.
            if (silence > 1.0) gainDb = std::min (gainDb, 6.0);
            if (! haveEstimate || silence > 4.0 || newQuieter)
            {
                // New content (first sound, after a long silence, or much quieter): start from here.
                pSlow = pFast = z;
                haveEstimate = true;
                activeTime = 0;
                quietRun = 0;
            }
            else
            {
                const double tauSlow = activeTime < 2.0 ? 0.8 : 3.0;
                pSlow += (1 - std::exp (-dur / tauSlow)) * (z - pSlow);
                pFast += (1 - std::exp (-dur / 0.3)) * (z - pFast);
            }
            silence = 0;
            activeTime += dur;
        }
        else silence += dur;

        if (haveEstimate)
        {
            const double slow = powerToLufs (pSlow), fast = powerToLufs (pFast);
            double desired = std::clamp (s.targetLufs - slow, -s.maxCutDb, s.maxBoostDb);
            const double guard = s.targetLufs + s.jumpGuardDb - fast;   // how far a jump may overshoot
            const bool jump = desired > guard && guard < gainDb;
            if (desired > guard) desired = std::max (guard, -s.maxCutDb);
            double rate;
            if (desired < gainDb) rate = jump ? 40.0 : 6.0;
            else rate = active ? (activeTime < 2.0 ? 10.0 : s.riseDbPerSec) : 0.0;
            const double step = rate * dur;
            gainDb = desired < gainDb ? std::max (desired, gainDb - step) : std::min (desired, gainDb + step);
        }

        const double target = dbToGain (gainDb);
        const double g0 = appliedGain, dg = (target - g0) / n;
        for (int i = 0; i < n; ++i)
        {
            float* p = x + (size_t) i * (size_t) stride;
            const float g = (float) (g0 + dg * (i + 1));
            for (int c = 0; c < channels; ++c) p[c] *= g;
        }
        appliedGain = target;
    }

private:
    double fs = 48000;
    KWeight kw[2];
    double pSlow = 0, pFast = 0, silence = 0, quietRun = 0, activeTime = 0, gainDb = 0, appliedGain = 1;
    bool haveEstimate = false;
};

// Look-ahead peak limiter: the gain reaches its minimum exactly when the peak leaves the delay,
// with a smooth (boxcar) attack and an exponential release.
class Limiter
{
public:
    void prepare (double fs, int channels, double lookaheadMs = 1.5, double releaseMs = 80)
    {
        la = std::max (1, (int) std::lround (fs * lookaheadMs / 1000.0));
        nch = channels;
        delay.assign ((size_t) la * (size_t) nch, 0.0f);
        holdVal.assign ((size_t) la + 1, 1.0f);
        holdIdx.assign ((size_t) la + 1, 0);
        box.assign ((size_t) la, 1.0f);
        rel = 1.0 - std::exp (-1.0 / (fs * releaseMs / 1000.0));
        reset();
    }
    void reset()
    {
        std::fill (delay.begin(), delay.end(), 0.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        dpos = 0; t = 0; qh = qt = 0; bpos = 0; boxSum = la; gain = 1; minGain = 1;
    }
    int latency() const { return la; }
    float lastMinGain() { const float m = minGain; minGain = 1; return m; }

    void process (float* x, int n, int stride, float ceiling)
    {
        const int cap = la + 1;
        for (int i = 0; i < n; ++i, ++t)
        {
            float* s = x + (size_t) i * (size_t) stride;
            float peak = 0;
            for (int c = 0; c < nch; ++c) peak = std::max (peak, std::abs (s[c]));
            const float req = peak > ceiling ? ceiling / peak : 1.0f;
            // Sliding minimum of the required gain over the last la+1 samples (monotonic queue).
            while (qt != qh && holdVal[(size_t) ((qt - 1 + cap) % cap)] >= req) qt = (qt - 1 + cap) % cap;
            holdVal[(size_t) qt] = req; holdIdx[(size_t) qt] = t; qt = (qt + 1) % cap;
            while (holdIdx[(size_t) qh] <= t - cap) qh = (qh + 1) % cap;
            const float hold = holdVal[(size_t) qh];
            // Boxcar over la samples: reaches `hold` after la samples, i.e. when the peak comes out.
            boxSum += hold - box[(size_t) bpos];
            box[(size_t) bpos] = hold;
            bpos = (bpos + 1) % la;
            const float smooth = std::min (1.0f, (float) (boxSum / la));
            gain = smooth < gain ? smooth : gain + (float) rel * (smooth - gain);
            minGain = std::min (minGain, gain);
            // Delay the audio by la samples and apply: the sample leaving now entered la samples
            // ago, and every hold value in the boxcar window already includes it.
            for (int c = 0; c < nch; ++c)
            {
                float& d = delay[(size_t) dpos * (size_t) nch + (size_t) c];
                const float out = d;
                d = s[c];
                s[c] = std::clamp (out * gain, -ceiling, ceiling);
            }
            dpos = (dpos + 1) % la;
        }
    }

private:
    int la = 72, nch = 2, dpos = 0, qh = 0, qt = 0, bpos = 0;
    long long t = 0;
    std::vector<float> delay, holdVal, box;
    std::vector<long long> holdIdx;
    double boxSum = 0, rel = 0.01;
    float gain = 1, minGain = 1;
};
} // namespace rv
