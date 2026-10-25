#pragma once
// How each mode sounds before the user touches anything, per kind of output device.
// The ASUS MX34VQ values come from the user's FxSound presets that were tuned and checked for it
// ("MX34VQ Music v2" for music, "MX34VQ Speech" for speech); EQ is stored relative to the mids,
// since the leveler sets the level anyway.
#include "../brain/TitleRules.h"
#include "../dsp/Shared.h"
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

enum class DeviceKind { Generic, MX34VQ };

inline DeviceKind deviceKindFor (const std::wstring& name)
{
    return name.find (L"MX34VQ") != std::wstring::npos ? DeviceKind::MX34VQ : DeviceKind::Generic;
}

inline ModeSound defaultSound (DeviceKind device, Kind kind)
{
    // Loudness behaviour per mode (measured on a playlist of speech, music, ads and quiet videos):
    //  speech: brings quiet voices up a lot and fast, pulls jumps back hard
    //  music: leaves a chorus louder than a verse, never pumps quiet parts of a song
    //  film: in between; games: keeps footsteps quiet and explosions loud
    if (device == DeviceKind::MX34VQ)
    {
        switch (kind)
        {
            case Kind::Speech: return { 50, 0, 0, 0, { 0, -2.3f, -5.3f, -1.6f, 0, 0, 0, -1.7f, -2.0f, -1.3f }, 20, 1.5f, 1.0f, 3, 0, 0.6f };
            case Kind::Movie: return { 60, 0, 15, 12, { 0, 0.5f, -2.5f, -0.8f, 0, 0, 0.5f, -1.0f, -1.5f, -1.0f }, 15, 3, 2.5f, 2, -2, 0.5f };
            case Kind::Game: return { 40, 0, 30, 15, { 0, 1.0f, -1.0f, 0, 0, 0, 0.5f, 0, -1.0f, -1.0f }, 9, 6, 6, 1, -2, 0 };
            default: return { 80, 21, 22, 20, { 0, 2.2f, -0.8f, 0, 0, 0, 0, -1.2f, -1.5f, -0.8f }, 12, 4, 4, 2, 0, 0.5f };
        }
    }
    // Tone correction strengths come from tools/eval (damaged clips): speech 0.6 with a 3.5 dB dead
    // zone, music 0.5 with 4 dB, so good recordings are barely touched.
    switch (kind)
    {
        case Kind::Speech: return { 30, 0, 0, 0, { 0, -1.0f, -2.5f, -1.0f, 0, 0, 0.5f, 0.5f, 0, 0 }, 20, 1.5f, 1.0f, 3, 0, 0.6f };
        case Kind::Movie: return { 30, 0, 10, 10, { 0, 0, -1.0f, -0.5f, 0, 0, 0.5f, 0, 0, 0 }, 15, 3, 2.5f, 2, -2, 0.5f };
        case Kind::Game: return { 20, 0, 20, 10, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 9, 6, 6, 1, -2, 0 };
        default: return { 40, 0, 10, 10, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 12, 4, 4, 2, 0, 0.5f };
    }
}

// What the user changed on top of a mode (sliders 0..1 map to FxSound's 0..127; EQ adds).
struct ModeTweaks
{
    float fidelity = -1, ambience = -1, surround = -1, bass = -1;   // -1 = use the mode's own value
    float eq[10] {};
};

// The volume slider (0..1) sets the loudness everything is brought to.
inline float targetLufsFor (float volume) { return -22.0f + 16.0f * volume; }   // -22 .. -6 LUFS
} // namespace rv
