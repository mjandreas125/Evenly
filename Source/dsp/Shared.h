#pragma once
// What the app and the audio engine (inside audiodg) share through the file mapping
// "Global\Rovno.<endpoint guid>": the app writes EngineParams, the engine writes Telemetry.
// Plain data only; the layout is versioned.
#include <atomic>
#include <cstdint>
#include <cstring>

namespace rv
{
constexpr uint32_t kSharedMagic = 0x4F4E5652;   // "RVNO"
constexpr uint32_t kSharedVersion = 1;
constexpr int kBands = 26;                       // third octaves, 50 Hz .. 16 kHz
constexpr float kBandHz[kBands] = { 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
                                    1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
constexpr int kFrameRing = 128;                  // ~5.5 s of analysis frames

enum class LoudnessMode : uint32_t { Off = 0, Leveler = 1, FxSoundBoost = 2 };

struct EngineParams
{
    uint32_t enabled = 1;
    // FxSound effects, MIDI 0..127 exactly as stored in FxSound presets.
    int32_t fidelity = 0, ambience = 0, surround = 0, bass = 0, boost = 0;
    float eqDb[10] {};           // graphic EQ (FxSound band layout and filter shape)
    float toneDb[10] {};         // automatic correction: device + content + what was learned for the source
    uint32_t loudness = (uint32_t) LoudnessMode::Leveler;
    float targetLufs = -16.0f;
    float maxBoostDb = 12.0f, maxCutDb = 18.0f;
    float riseDbPerSec = 2.0f;
    float jumpGuardDb = 3.0f;    // music needs room for a louder chorus; speech and video much less
    float quietToNewSec = 2.0f;  // a quiet stretch this long counts as new, quieter content
    float ceilingDb = -1.0f;
    float outputDb = 0.0f;
    uint32_t reserved[16] {};
};

struct AnalysisFrame
{
    uint32_t seq;                // frame number + 1; 0 = empty
    float lufs;                  // K-weighted loudness of this frame
    float peak;
    float bandsDb[kBands];       // input spectrum (before any processing), power per third octave
};

struct Telemetry
{
    uint32_t heartbeat;          // incremented on every processed buffer
    uint32_t engineBuild;        // kSharedVersion of the running effect
    uint32_t paramsSource;       // 0 = defaults, 1 = saved file, 2 = app (shared memory)
    uint32_t sampleRate, channels;
    float inMomentary, inShortTerm, outShortTerm;
    float levelerGainDb, limiterMinGainDb, outPeak;
    uint32_t framesWritten;
    AnalysisFrame frames[kFrameRing];
    uint32_t reserved[16];
};

struct SharedBlock
{
    uint32_t magic, version;
    std::atomic<uint32_t> paramSeq;   // seqlock: odd while the app is writing params
    uint32_t paramsWritten;           // 1 once the app has written params at least once
    EngineParams params;
    Telemetry telemetry;
};

// Seqlock helpers. Writer (app): writeParams(). Reader (real-time audio): tryReadParams().
inline void writeParams (SharedBlock& b, const EngineParams& p)
{
    b.paramSeq.fetch_add (1, std::memory_order_acq_rel);
    std::atomic_thread_fence (std::memory_order_release);
    std::memcpy (&b.params, &p, sizeof (p));
    std::atomic_thread_fence (std::memory_order_release);
    b.paramSeq.fetch_add (1, std::memory_order_acq_rel);
    b.paramsWritten = 1;
}

inline bool tryReadParams (const SharedBlock& b, EngineParams& out)
{
    const uint32_t s1 = b.paramSeq.load (std::memory_order_acquire);
    if (s1 & 1u) return false;
    EngineParams tmp;
    std::memcpy (&tmp, &b.params, sizeof (tmp));
    std::atomic_thread_fence (std::memory_order_acquire);
    if (b.paramSeq.load (std::memory_order_acquire) != s1) return false;
    out = tmp;
    return true;
}
} // namespace rv
