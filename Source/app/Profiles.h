#pragma once
// How each mode sounds before the user touches anything, per kind of output device.
// The ASUS MX34VQ values come from the user's FxSound presets that were tuned and checked for it
// ("MX34VQ Music v2" for music, "MX34VQ Speech" for speech); EQ is stored relative to the mids,
// since the leveler sets the level anyway.
#include "../brain/TitleRules.h"
#include "../dsp/Shared.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace rv
{
struct ModeSound
{
    int fidelity, ambience, surround, bass;      // FxSound effects, 0..127
    float eq[10];
    float maxBoostDb, jumpGuardDb, quietToNewSec, riseDbPerSec, targetOffsetDb;
    float toneStrength;                          // automatic tone correction; 0 = off for this mode
};

enum class DeviceKind { Generic, MX34VQ, Headphones };

inline DeviceKind deviceKindFor (const std::wstring& name)
{
    return name.find (L"MX34VQ") != std::wstring::npos ? DeviceKind::MX34VQ : DeviceKind::Generic;
}

inline ModeSound defaultSound (DeviceKind device, Kind kind)
{
    // Loudness behaviour per mode (measured on a playlist of speech, music, ads and quiet videos):
    //  speech: brings quiet voices up a lot and fast, pulls jumps back hard
    //  music: leaves a chorus louder than a verse, never pumps quiet parts of a song (03.11.2026: rising 2 dB/s took a quiet
    //         intro up 10 dB and crept back up after every loud part - the user heard the volume jumping; now 0.5 dB/s, at
    //         most +8 dB, and 6 dB of room for a louder chorus before it is pulled down fast)
    //  film: in between; games: keeps footsteps quiet and explosions loud
    //  19.11.2026, at the user's request "everything equally loud at once": music, speech and film all level fast - a quiet part
    //  is up within ~2 s (was never for a quiet verse after a loud chorus), a loud part is held at the target from the first
    //  second (was +5 dB for 2 s): rise 6 dB/s, 1.5 dB of room, quieter content after 0.5 s, music up to +24 dB. Measured with
    //  C:\devoicetest\probe\levsim.exe (the leveler on a made-up programme); games keep their dynamics.
    if (device == DeviceKind::MX34VQ)
    {
        switch (kind)
        {
            // 07.11.2026: speech and film may rise up to +36 / +30 dB: a video with the player at 10% arrives at -44 LUFS and
            // with +20 dB stayed far under the target line.
            case Kind::Speech: return { 50, 0, 0, 0, { 0, -2.3f, -5.3f, -1.6f, 1, 1, 1, -1.7f, -2.0f, -1.3f }, 36, 1.5f, 0.6f, 6, 0, 0.6f };
            case Kind::Movie: return { 60, 0, 15, 12, { 0, 0.5f, -2.5f, -0.8f, 0, 0, 0.5f, -1.0f, -1.5f, -1.0f }, 30, 1.5f, 0.6f, 6, -2, 0.5f };
            case Kind::Game: return { 40, 0, 30, 15, { 0, 1.0f, -1.0f, 0, 0, 0, 0.5f, 0, -1.0f, -1.0f }, 9, 6, 6, 1, -2, 0 };
            default: return { 80, 21, 22, 20, { 0, 2.2f, -0.8f, 0, 0, 0, 0, -1.2f, -1.5f, -0.8f }, 24, 1.5f, 0.5f, 6, 0, 0.5f };
        }
    }
    // Headphones sit on the ears: a little lower target (so a loud passage is not pushed up), no big boosts.
    if (device == DeviceKind::Headphones)
    {
        switch (kind)
        {
            case Kind::Speech: return { 30, 0, 0, 0, { 0, -1.0f, -2.0f, -1.0f, 0, 0, 0.5f, 0.5f, 0, 0 }, 16, 1.5f, 0.6f, 6, -2, 0.6f };
            case Kind::Movie: return { 30, 0, 10, 10, { 0, 0, -1.0f, -0.5f, 0, 0, 0.5f, 0, 0, 0 }, 12, 1.5f, 0.6f, 6, -4, 0.5f };
            case Kind::Game: return { 20, 0, 20, 10, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 9, 6, 6, 1, -4, 0 };
            default: return { 40, 0, 10, 10, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 16, 1.5f, 0.5f, 6, -2, 0.5f };
        }
    }
    // Tone correction strengths come from tools/eval (damaged clips): speech 0.6 with a 3.5 dB dead
    // zone, music 0.5 with 4 dB, so good recordings are barely touched.
    switch (kind)
    {
        case Kind::Speech: return { 30, 0, 0, 0, { 0, -1.0f, -2.5f, -1.0f, 0, 0, 0.5f, 0.5f, 0, 0 }, 20, 1.5f, 0.6f, 6, 0, 0.6f };
        case Kind::Movie: return { 30, 0, 10, 10, { 0, 0, -1.0f, -0.5f, 0, 0, 0.5f, 0, 0, 0 }, 15, 1.5f, 0.6f, 6, -2, 0.5f };
        case Kind::Game: return { 20, 0, 20, 10, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 9, 6, 6, 1, -2, 0 };
        default: return { 40, 0, 10, 10, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 24, 1.5f, 0.5f, 6, 0, 0.5f };
    }
}

// What the user changed on top of a mode (sliders 0..1 map to FxSound's 0..127; EQ adds).
struct ModeTweaks
{
    float fidelity = -1, ambience = -1, surround = -1, bass = -1;   // -1 = use the mode's own value
    float eq[10] {};
};

// Where the volume slider starts for an output the app has not seen yet.
inline float defaultVolumeFor (DeviceKind d) { return d == DeviceKind::Headphones ? 0.65f : 0.8f; }

// The loudness the leveler brings everything to (0..1 on the old slider scale: -22 .. -6 LUFS).
inline float targetLufsFor (float volume) { return -22.0f + 16.0f * volume; }

// The volume slider (0..1) is a gain on the very output, after the leveler, boost and limiter.
// (07.11.2026: it used to move the leveler's target, but on speakers FxSound's Dynamic Boost after the leveler
// holds its own output level, so the slider changed almost nothing.) 100% = 0 dB, 50% = -12 dB, 0 = -40 dB.
inline float volumeDbFor (float volume) { return volume <= 0.01f ? -40.0f : std::max (-40.0f, 40.0f * std::log10 (volume)); }
} // namespace rv
