#pragma once
// The whole processing chain, shared by the audio effect inside Windows and the offline tools:
//   input analysis -> 10-band EQ (user EQ + automatic tone correction, gains glide)
//   -> FxSound effects (Fidelity, Ambience, Surround, Bass)
//   -> loudness: Rovno leveler + limiter, or FxSound's own Dynamic Boost (exact FxSound sound)
//   -> output trim.
// Real-time safe after prepare(): no allocation, no locks.
#include "Analyzer.h"
#include "FxCore.h"
#include "Loudness.h"
#include "Shared.h"

namespace rv
{
class Engine
{
public:
    void prepare (double sampleRate, int channels)
    {
        fs = sampleRate;
        nch = std::max (1, channels);
        fxp = fx::makeParams ({}, (float) fs);   // also builds the static tables off the audio thread
        lex.prepare ((float) fs, fxp.lex.roomsize);
        wide.prepare ((float) fs);
        maxi.prepare ((float) fs);
        aural.reset(); bass.reset();
        leveler.prepare (fs);
        limiter.prepare (fs, nch);
        inMeter.prepare (fs); outMeter.prepare (fs);
        analyzer.prepare (fs);
        scratch.assign ((size_t) (std::max (16, (int) (fs * 0.01)) + 1) * (size_t) nch, 0.0f);
        for (auto& g : eqNow) g = 0;
        eqFlat();
        firstParams = true;
        applyParams (params);
        firstParams = true;   // the first setParams() after prepare() also takes effect at once
    }

    // Takes new parameters; cheap if nothing changed. Safe to call before every buffer.
    void setParams (const EngineParams& p)
    {
        if (std::memcmp (&p, &params, sizeof (p)) != 0 || firstParams) applyParams (p);
    }
    const EngineParams& currentParams() const { return params; }
    int latencySamples() const
    {
        if (params.loudness == (uint32_t) LoudnessMode::FxSoundBoost) return fxp.maxi.maxDelay;
        return limiter.latency() + (params.loudness == (uint32_t) LoudnessMode::Leveler && params.boost > 0 ? fxp.maxi.maxDelay : 0);
    }

    // In place, interleaved, `frames` x channels. Telemetry may be null.
    void process (float* x, int frames, Telemetry* tel)
    {
        if (tel != nullptr)
            analyzer.process (x, frames, nch, nch, [&] (const AnalysisFrame& f)
            {
                // seq = position in the shared stream (it outlives this engine instance), so the
                // reader can tell a finished slot from one being overwritten.
                auto& slot = tel->frames[tel->framesWritten % kFrameRing];
                slot.seq = 0;
                std::atomic_thread_fence (std::memory_order_release);
                std::memcpy (&slot.lufs, &f.lufs, sizeof (AnalysisFrame) - sizeof (uint32_t));
                std::atomic_thread_fence (std::memory_order_release);
                slot.seq = tel->framesWritten + 1;
                tel->framesWritten++;
            });
        inMeter.process (x, frames, nch, nch);

        if (params.enabled)
        {
            const int block = std::max (16, (int) (fs * 0.01));
            for (int pos = 0; pos < frames; pos += block)
            {
                const int n = std::min (block, frames - pos);
                float* const raw = x + (size_t) pos * (size_t) nch;
                // Below -80 dBFS (the keep-awake pulses of the output, the tail of a fade) nothing may be
                // amplified: the block is processed on a copy, so that all the state keeps running as if it
                // were silence, and the sound goes out as it came in.
                float peak = 0;
                for (int i = 0; i < n * nch; ++i) peak = std::max (peak, std::abs (raw[i]));
                const bool belowFloor = peak < 1.0e-4f && scratch.size() >= (size_t) n * (size_t) nch;
                float* p = raw;
                if (belowFloor) { std::memcpy (scratch.data(), raw, (size_t) n * (size_t) nch * sizeof (float)); p = scratch.data(); }
                glideEq (n / fs);
                if (nch >= 2)
                {
                    if (eq.active()) eq.process (p, n, nch);
                    if (fxp.aural.on) aural.process (p, n, nch, fxp);
                    if (fxp.lex.on) lex.process (p, n, nch, fxp);
                    if (fxp.wide.on) wide.process (p, n, nch, fxp);
                    if (fxp.bass.on) bass.process (p, n, nch, fxp);
                }
                switch ((LoudnessMode) params.loudness)
                {
                    case LoudnessMode::FxSoundBoost:
                        if (nch >= 2) maxi.process (p, n, nch, fxp);
                        break;
                    case LoudnessMode::Leveler:
                        leveler.process (p, n, nch, nch, levelerSettings);
                        // Optional FxSound Dynamic Boost after the leveler: the leveler gives it the same input level
                        // every time, the boost raises speech to FxSound's loudness (a plain peak limiter cannot,
                        // speech has too much headroom between its loudness and its peaks).
                        if (params.boost > 0 && nch >= 2) maxi.process (p, n, nch, fxp);
                        limiter.process (p, n, nch, ceiling);
                        break;
                    default:
                        limiter.process (p, n, nch, ceiling);
                        break;
                }
                if (outGain != 1.0f)
                    for (int i = 0; i < n * nch; ++i) p[i] *= outGain;
            }
        }
        outMeter.process (x, frames, nch, nch);

        if (tel != nullptr)
        {
            tel->sampleRate = (uint32_t) fs;
            tel->channels = (uint32_t) nch;
            tel->inMomentary = (float) inMeter.momentaryLufs();
            tel->inShortTerm = (float) inMeter.shortTermLufs();
            tel->outShortTerm = (float) outMeter.shortTermLufs();
            tel->levelerGainDb = params.loudness == (uint32_t) LoudnessMode::Leveler ? (float) leveler.gainDb_() : 0.0f;
            tel->limiterMinGainDb = (float) (20 * std::log10 (std::max (1e-6f, limiter.lastMinGain())));
            float pk = 0;
            for (int i = 0; i < frames * nch; ++i) pk = std::max (pk, std::abs (x[i]));
            tel->outPeak = pk;
            tel->heartbeat++;
        }
    }

private:
    void applyParams (const EngineParams& p)
    {
        const bool fxChanged = firstParams || p.fidelity != params.fidelity || p.ambience != params.ambience
                               || p.surround != params.surround || p.bass != params.bass || p.boost != params.boost;
        const bool loudChanged = ! firstParams && p.loudness != params.loudness;
        params = p;
        if (fxChanged) fxp = fx::makeParams ({ p.fidelity, p.ambience, p.surround, p.boost, p.bass }, (float) fs);
        for (int i = 0; i < 10; ++i) eqTarget[i] = std::clamp (p.eqDb[i] + p.toneDb[i], -18.0f, 18.0f);
        if (firstParams)
        {
            // The first settings take effect at once (no glide from flat).
            for (int i = 0; i < 10; ++i) { eqNow[i] = eqTarget[i]; setBand (i); }
        }
        levelerSettings = { p.targetLufs, p.maxBoostDb, p.maxCutDb, p.riseDbPerSec, p.jumpGuardDb, p.quietToNewSec };
        ceiling = (float) dbToGain (std::min (0.0f, p.ceilingDb));
        outGain = (float) dbToGain (std::clamp (p.outputDb, -40.0f, 12.0f));
        if (loudChanged) { leveler.reset(); limiter.reset(); maxi.reset(); }
        firstParams = false;
    }

    // EQ gains glide toward their targets at 6 dB/s, so automatic corrections never click.
    void glideEq (double dur)
    {
        const float step = (float) (6.0 * dur);
        for (int i = 0; i < 10; ++i)
        {
            if (eqNow[i] == eqTarget[i]) continue;
            eqNow[i] = eqTarget[i] > eqNow[i] ? std::min (eqTarget[i], eqNow[i] + step) : std::max (eqTarget[i], eqNow[i] - step);
            if (std::abs (eqNow[i] - eqSet[i]) >= 0.05f || eqNow[i] == eqTarget[i]) setBand (i);
        }
    }
    void setBand (int i)
    {
        eqSet[i] = eqNow[i];
        eq.setup ((float) fs, eqSet);   // recomputes the ten sections; state is kept
    }
    void eqFlat() { for (auto& g : eqSet) g = 0; eq.setup ((float) fs, eqSet); eq.reset(); }

    double fs = 48000;
    int nch = 2;
    EngineParams params {};
    bool firstParams = true;
    fx::Params fxp {};
    fx::GraphicEq eq;
    fx::Aural aural;
    fx::Lex lex;
    fx::Wide wide;
    fx::Bass bass;
    fx::Maximizer maxi;
    Leveler leveler;
    LevelerSettings levelerSettings;
    Limiter limiter;
    LoudnessMeter inMeter, outMeter;
    Analyzer analyzer;
    std::vector<float> scratch;
    float eqTarget[10] {}, eqNow[10] {}, eqSet[10] {};
    float ceiling = 0.891f, outGain = 1.0f;
};
} // namespace rv
