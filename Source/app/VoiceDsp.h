#pragma once
// What the singing voice is doing, taken from the mixed sound with plain signal processing (no neural network):
//   * the pitch of the strongest voiced sound between ~80 and ~800 Hz (YIN: the cheapest honest pitch detector), after the sound has been
//     limited to 200 Hz..3 kHz so that bass, kick and cymbals do not get in the way of the voice;
//   * from the pitch over time: how high the voice is in its own range, whether it climbs or falls, whether a note is HELD (a stretched
//     vowel), vibrato, where a phrase begins and ends and where it is heading, how hard it is sung (loudness, brightness) and the
//     tension that comes out of those.
// Pure C++ (no JUCE, no Windows), so it can be tested with made-up voices.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <deque>
#include <vector>

namespace rv
{
struct VoiceFeatures
{
    float voiced = 0;        // 0..1 how sure we are that a voice is singing right now
    float midi = 57;         // pitch in semitones (A4 = 69), the last one while voiced
    float reg = 0.5f;        // 0..1 where the pitch sits in the singer's own range of the last ~12 s
    float slope = 0;         // semitones per second, + climbing / - falling (smoothed)
    float hold = 0;          // 0..1 a note is being held (the vowel is stretched)
    float vibrato = 0;       // 0..1
    float build = 0;         // -1..1 how far this phrase has climbed (+) or fallen (-) since it began
    float intensity = 0;     // 0..1 how hard it is sung, against the singer's own loudest in the last ~8 s
    float bright = 0;        // 0..1 how much of the voice is in the high harmonics (shouting / edge / breath)
    float tension = 0;       // 0..1 all of the above as one number: tender (0) .. strained (1)
    float phraseAge = 0;     // seconds since the phrase began, 0 between phrases
    float endPulse = 0;      // 1 the moment a phrase ends, falls to 0 in about a second
    float phraseLen = 0;     // seconds of the phrase that has just ended (while endPulse > 0)
    // sharp sounds: every time a sound STARTS abruptly (a syllable, a hard consonant, a hit) onsetSeq goes up by one
    uint32_t onsetSeq = 0;
    float onsetStrength = 0; // 0..1 how hard the last one was
    float onsetHigh = 0;     // 0..1 how hissy / sharp it was (s, t, k, ch..) against how round (vowel, b, m..)
};

class VoiceDsp
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 12000.0));
        fd = fs / decim;
        hop = std::max (1, (int) std::lround (fd * 0.020));          // 50 frames a second
        winLen = 768;
        tauMin = std::max (2, (int) (fd / 800.0));
        tauMax = std::min (winLen - 513, (int) (fd / 80.0));
        blkLen = std::max (8, (int) std::lround (fs * 0.010));   // 10 ms blocks (shorter ones ripple with the pitch itself) for the detection of sharp sounds
        hp.design (fs, 200.0, true);  hp2.design (fs, 200.0, true);
        lp.design (fs, 3000.0, false); lp2.design (fs, 3000.0, false);
        hi.design (fs, 1500.0, true);
        ring.assign ((size_t) winLen, 0.0f);
        std::fill (ring.begin(), ring.end(), 0.0f);
        cm.assign ((size_t) tauMax + 2, 0.0f);
        reset();
    }
    void reset()
    {
        filled = 0; sinceHop = 0; accN = 0; accE = accH = 0;
        f = {}; hist.clear(); regHist.clear();
        blkN = 0; blkV = blkH = 0; slowLog = -9.0f; fastLog = -9.0f; refractory = 0; onSeq = 0; onStr = onHi = 0; blkMax = 1e-6f;
        lastMidi = 0; cand = 0; candN = 0; unvoicedFor = 9.0f; slopeS = 0; held = 0; slowMidi = 0; phraseStartMidi = 0; inPhrase = false;
        phraseT = 0; rmsMax = 1e-4f; buildS = 0; vibS = 0; voicedS = 0; intS = 0; brightS = 0;
    }

    // mono samples at the input rate; returns how many analysis frames were made
    int push (const float* x, int n)
    {
        int frames = 0;
        for (int i = 0; i < n; ++i)
        {
            const float s = lp2.run (lp.run (hp2.run (hp.run (x[i]))));
            const float h = hi.run (x[i]);
            accH += h * h;
            blkV += s * s; blkH += h * h;
            if (++blkN >= blkLen) { blockDone(); }
            accSum += s;
            if (++accN == decim)
            {
                // one sample of the decimated stream (the 3 kHz low-pass above is also the anti-alias filter)
                ring.erase (ring.begin());
                ring.push_back (accSum / (float) decim);
                accSum = 0; accN = 0;
                accE += ring.back() * ring.back();
                if (filled < winLen) ++filled;
                if (++sinceHop >= hop) { sinceHop = 0; if (filled >= winLen) { frame(); ++frames; } }
            }
        }
        return frames;
    }
    const VoiceFeatures& features() const { return f; }
    float dbgMin = 0, dbgRel = 0, dbgRms = 0, dbgHeld = 0, dbgLast = 0, dbgSlow = 0; int dbgBest = 0;   // (for the test)

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void design (double fs, double fc, bool high)
        {
            const double w = 2 * 3.14159265358979 * fc / fs, c = std::cos (w), s = std::sin (w), q = 0.7071, al = s / (2 * q);
            const double a0 = 1 + al;
            const double B0 = high ? (1 + c) / 2 : (1 - c) / 2, B1 = high ? -(1 + c) : (1 - c), B2 = B0;
            b0 = (float) (B0 / a0); b1 = (float) (B1 / a0); b2 = (float) (B2 / a0); a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
            z1 = z2 = 0;
        }
        float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };

    static float clamp01 (float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

    void frame()
    {
        const float dt = (float) (hop * decim / fs);
        const float* x = ring.data();
        const int W = 512;
        // YIN: cumulative mean normalised difference
        float run = 0;
        cm[0] = 1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0;
            for (int j = 0; j < W; ++j) { const float e = x[j] - x[j + tau]; d += e * e; }
            run += d;
            cm[(size_t) tau] = run > 1e-12f ? d * tau / run : 1.0f;
        }
        // Several periods may fit (the voice, but also a bass guitar or a chord): take all the clear ones and prefer the one that sits where
        // this singer has been singing, and in the usual range of a voice. The very first clear one is no longer simply the winner.
        float refMidi = 0;
        if (regHist.size() > 40) { std::vector<float> v (regHist.begin(), regHist.end()); std::nth_element (v.begin(), v.begin() + v.size() / 2, v.end()); refMidi = v[v.size() / 2]; }
        int best = -1;
        float bestScore = -1e9f;
        for (int tau = tauMin + 1; tau < tauMax; ++tau)
        {
            const float v = cm[(size_t) tau];
            if (v >= 0.32f || v > cm[(size_t) tau - 1] || v > cm[(size_t) tau + 1]) continue;
            const float m = 69.0f + 12.0f * std::log2 ((float) (fd / tau) / 440.0f);
            float score = (0.32f - v) * 3.0f;
            if (m >= 50.0f && m <= 76.0f) score += 0.10f;                       // the range of most singing voices
            if (refMidi > 0) score -= 0.035f * std::abs (m - refMidi);          // near where the voice has been
            if (lastMidi > 0 && unvoicedFor < 0.12f) score -= 0.02f * std::abs (m - lastMidi);   // and near the note it is on
            if (score > bestScore) { bestScore = score; best = tau; }
        }
        float minv = 1.0f;
        if (best < 0)
        {
            for (int tau = tauMin; tau <= tauMax; ++tau)
                if (cm[(size_t) tau] < minv) { minv = cm[(size_t) tau]; best = tau; }
        }
        else minv = cm[(size_t) best];
        float tauF = (float) best;
        if (best > tauMin && best < tauMax)
        {
            const float a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
            const float den = a - 2 * b + c;
            if (std::abs (den) > 1e-9f) tauF += 0.5f * (a - c) / den;
        }
        const float rms = std::sqrt (accE / (float) (hop > 0 ? hop : 1));
        const float rmsAll = std::sqrt (accH / (float) (hop * decim));
        accE = 0; accH = 0;
        rmsMax = std::max (rms, rmsMax * (1.0f - dt * 0.12f));
        const float rel = rms / std::max (rmsMax, 1e-6f);

        // is this a voice at all: a clear period and enough sound compared with the loudest recently
        dbgMin = minv; dbgRel = rel; dbgRms = rms; dbgBest = best;
        const float conf = clamp01 ((0.42f - minv) / 0.30f);
        const bool ok = best > 0 && conf > 0.25f && rel > 0.08f;
        float midi = 0;
        if (ok) midi = 69.0f + 12.0f * std::log2 ((float) (fd / tauF) / 440.0f);

        // an octave jump has to be confirmed by a few frames in a row, a single odd frame is held back
        bool accept = ok;
        if (ok && lastMidi > 0 && unvoicedFor < 0.12f && std::abs (midi - lastMidi) > 6.0f)
        {
            if (candN > 0 && std::abs (midi - cand) < 1.5f) ++candN; else { cand = midi; candN = 1; }
            if (candN < 4) accept = false; else candN = 0;
        }
        else candN = 0;

        if (accept)
        {
            lastMidi = midi;
            unvoicedFor = 0;
            if (slowMidi <= 0 || std::abs (midi - slowMidi) > 2.0f) { slowMidi = midi; held = 0; }      // a new note: start counting again
            else slowMidi += (midi - slowMidi) * std::min (1.0f, dt * 2.5f);
        }
        else unvoicedFor += dt;
        const bool voiced = accept || unvoicedFor < 0.08f;       // a gap of a few frames is still the same sound

        voicedS += ((voiced ? conf * 0.6f + 0.4f : 0.0f) - voicedS) * std::min (1.0f, dt * (voiced ? 14.0f : 8.0f));
        f.voiced = voicedS;
        f.onsetSeq = onSeq; f.onsetStrength = onStr; f.onsetHigh = onHi;
        if (voiced && lastMidi > 0) f.midi = f.midi + (lastMidi - f.midi) * std::min (1.0f, dt * 12.0f);

        if (voiced && lastMidi > 0) { medBuf.push_back (lastMidi); if (medBuf.size() > 5) medBuf.pop_front(); } else medBuf.clear();
        float lastMed = lastMidi;
        if (medBuf.size() >= 3) { std::vector<float> mv (medBuf.begin(), medBuf.end()); std::nth_element (mv.begin(), mv.begin() + mv.size() / 2, mv.end()); lastMed = mv[mv.size() / 2]; }
        hist.push_back (voiced ? lastMed : 0.0f);
        if (hist.size() > 40) hist.pop_front();
        if (accept) { regHist.push_back (midi); if (regHist.size() > 600) regHist.pop_front(); }

        // register: where the voice is inside its own range
        if (regHist.size() > 40)
        {
            std::vector<float> v (regHist.begin(), regHist.end());
            std::sort (v.begin(), v.end());
            float lo = v[v.size() / 10], hiP = v[v.size() * 9 / 10];
            if (hiP - lo < 6.0f) { const float m = 0.5f * (lo + hiP); lo = m - 3.0f; hiP = m + 3.0f; }
            f.reg += (clamp01 ((f.midi - lo) / (hiP - lo)) - f.reg) * std::min (1.0f, dt * 6.0f);
        }

        // slope over the last ~0.25 s
        float sl = 0;
        {
            const int n = (int) std::min<size_t> (hist.size(), 12);
            float sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
            for (int i = 0; i < n; ++i)
            {
                const float y = hist[hist.size() - (size_t) n + (size_t) i];
                if (y <= 0) continue;
                const float t = (float) i * dt;
                sx += t; sy += y; sxx += t * t; sxy += t * y; ++m;
            }
            if (m >= 6) { const float den = m * sxx - sx * sx; if (den > 1e-9f) sl = (m * sxy - sx * sy) / den; }
        }
        sl = std::max (-14.0f, std::min (14.0f, sl));
        slopeS += (sl - slopeS) * std::min (1.0f, dt * 6.0f);
        f.slope = slopeS;

        // held note: the pitch stays within a semitone (vibrato included) for a while
        if (voiced && lastMidi > 0 && std::abs (lastMidi - slowMidi) < 1.3f) held += dt; else if (voiced) held = std::max (0.0f, held - dt * 1.2f); else held = std::max (0.0f, held - dt * 4.0f);
        dbgHeld = held; dbgLast = lastMidi; dbgSlow = slowMidi;
        f.hold += (clamp01 ((held - 0.30f) / 1.4f) * (voiced ? 1.0f : 0.0f) - f.hold) * std::min (1.0f, dt * 5.0f);

        // vibrato: the pitch wobbles 4..8 times a second by a fraction of a semitone
        {
            const int n = (int) std::min<size_t> (hist.size(), 30);
            float mean = 0; int m = 0;
            for (int i = 0; i < n; ++i) { const float y = hist[hist.size() - (size_t) n + (size_t) i]; if (y > 0) { mean += y; ++m; } }
            float vib = 0;
            if (m >= 24)
            {
                mean /= (float) m;
                int cross = 0; float prevS = 0, var = 0;
                for (int i = 0; i < n; ++i)
                {
                    const float y = hist[hist.size() - (size_t) n + (size_t) i];
                    if (y <= 0) continue;
                    const float d = y - mean; var += d * d;
                    if (prevS != 0 && ((prevS < 0) != (d < 0))) ++cross;
                    prevS = d;
                }
                const float rate = (float) cross / (2.0f * (float) n * dt);
                const float amp = std::sqrt (var / (float) m);
                if (rate > 3.5f && rate < 8.5f && std::abs (slopeS) < 3.0f) vib = clamp01 ((amp - 0.05f) / 0.28f);
            }
            vibS += (vib - vibS) * std::min (1.0f, dt * 4.0f);
            f.vibrato = vibS;
        }

        // phrases: a voice that starts after a pause of a third of a second; ends the same way
        if (voiced)
        {
            if (! inPhrase) { inPhrase = true; phraseT = 0; phraseStartMidi = lastMidi; buildS = 0; }
            phraseT += dt;
        }
        else if (inPhrase && unvoicedFor > 0.35f)
        {
            inPhrase = false;
            if (phraseT > 0.6f) { f.endPulse = 1.0f; f.phraseLen = phraseT; }
            phraseT = 0;
        }
        f.phraseAge = inPhrase ? phraseT : 0.0f;
        f.endPulse = std::max (0.0f, f.endPulse - dt * 1.1f);
        const float bt = inPhrase && lastMidi > 0 ? std::max (-1.0f, std::min (1.0f, (lastMidi - phraseStartMidi) / 6.0f)) : 0.0f;
        buildS += (bt - buildS) * std::min (1.0f, dt * 3.0f);
        f.build = buildS;

        // how hard, how edgy, and the tension that results
        const float intens = clamp01 (std::pow (rel, 0.7f));
        intS += (intens - intS) * std::min (1.0f, dt * 5.0f);
        const float br = clamp01 ((rmsAll > 1e-7f ? std::sqrt (accHigh (rmsAll, rms)) : 0.0f) * 3.0f);
        brightS += (br - brightS) * std::min (1.0f, dt * 5.0f);
        f.intensity = intS * (voiced ? 1.0f : 0.5f);
        f.bright = brightS;
        const float tn = 0.40f * f.reg + 0.30f * intS + 0.15f * brightS + 0.15f * clamp01 (std::abs (slopeS) / 6.0f);
        f.tension += (tn - f.tension) * std::min (1.0f, dt * 1.5f);
    }
    // 5 ms at a time: has a sound just started, and how sharply? (the loudness jumps well above its own recent average)
    void blockDone()
    {
        const float ev = std::sqrt (blkV / (float) blkN), eh = std::sqrt (blkH / (float) blkN);
        blkN = 0; blkV = blkH = 0;
        blkMax = std::max (ev, blkMax * 0.9995f);
        const float lg = std::log (ev + 1e-5f);
        if (slowLog < -8.0f) { slowLog = fastLog = lg; return; }
        const float dtb = (float) (blkLen / fs);
        fastLog += (lg - fastLog) * std::min (1.0f, dtb / 0.015f);
        const float jump = fastLog - slowLog;                     // nepers above the average of the last ~250 ms
        slowLog += (lg - slowLog) * std::min (1.0f, dtb / 0.25f);
        if (refractory > 0) { refractory -= dtb; return; }
        if (jump > 1.05f && ev > 0.06f * blkMax)
        {
            onSeq++;
            onStr = std::min (1.0f, (jump - 1.05f) / 1.3f) * (0.7f + 0.3f * voicedS);
            onHi = std::min (1.0f, eh / (ev + 1e-6f) * 1.6f);
            refractory = 0.14f;
        }
    }
    static float accHigh (float high, float mid) { return mid > 1e-9f ? std::min (1.0f, (high * high) / (high * high + mid * mid * 4.0f + 1e-12f)) : 0.0f; }

    double fs = 48000, fd = 12000;
    int decim = 4, hop = 240, winLen = 768, tauMin = 15, tauMax = 150;
    Biquad hp, hp2, lp, lp2, hi;
    std::vector<float> ring, cm;
    int filled = 0, sinceHop = 0, accN = 0;
    float accSum = 0, accE = 0, accH = 0;
    VoiceFeatures f;
    std::deque<float> hist, regHist, medBuf;
    float lastMidi = 0, cand = 0, unvoicedFor = 9.0f, slopeS = 0, held = 0, slowMidi = 0, phraseStartMidi = 0, phraseT = 0, rmsMax = 1e-4f;
    float buildS = 0, vibS = 0, voicedS = 0, intS = 0, brightS = 0;
    int candN = 0;
    bool inPhrase = false;
    int blkLen = 240, blkN = 0;
    float blkV = 0, blkH = 0, slowLog = -9.0f, fastLog = -9.0f, refractory = 0, blkMax = 1e-6f, onStr = 0, onHi = 0;
    uint32_t onSeq = 0;
};
} // namespace rv
