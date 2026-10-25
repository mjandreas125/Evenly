#pragma once
// Puts the hints together into one decision: music, speech, film or game. Order of trust:
//   1. the mode the user picked by hand (auto off)
//   2. what the user picked earlier for this source (channel, artist or program)
//   3. what was learned about this source before
//   4. program and title words
//   5. the sound itself (needs a few seconds)
// The sound alone switches the mode only after it has been sure for a while, so the mode does
// not flicker; a new video or track switches at once.
#include "TitleRules.h"
#include <string>

namespace rv
{
enum class Why : int { None, Manual, ManualBefore, Learned, App, Title, Sound, Default };

struct Decision
{
    Kind kind = Kind::Music;
    Why why = Why::Default;
    std::wstring detail;         // the word or program behind a title/app decision
};

struct SourceMemory              // what the learner knows about the current source
{
    Kind manual = Kind::Unknown; // picked by hand for this source earlier
    Kind learned = Kind::Unknown;
    int votes = 0;               // how many times it was confirmed
};

class Decider
{
public:
    // Call about once a second. speechP < 0 = the sound has not been heard long enough yet.
    Decision update (const std::wstring& sourceKey, Kind manualNow, const SourceMemory& mem, const Guess& hint, float speechP)
    {
        const bool newSource = sourceKey != lastKey;
        if (newSource) { lastKey = sourceKey; pending = Kind::Unknown; pendingTicks = 0; soundP = -1; }
        if (speechP >= 0) soundP = soundP < 0 ? speechP : soundP + 0.25f * (speechP - soundP);

        Decision d;
        if (manualNow != Kind::Unknown) d = { manualNow, Why::Manual, {} };
        else if (mem.manual != Kind::Unknown) d = { mem.manual, Why::ManualBefore, {} };
        else if (mem.learned != Kind::Unknown && mem.votes >= 3) d = { mem.learned, Why::Learned, {} };
        else d = fromHints (hint);

        // Settled decisions (hand-picked, learned, strong hints) apply at once; the sound alone
        // has to agree with itself for 4 s before it changes the mode.
        if (d.why == Why::Sound && ! newSource && current.why != Why::Default)
        {
            if (d.kind != current.kind)
            {
                if (d.kind == pending) ++pendingTicks; else { pending = d.kind; pendingTicks = 1; }
                if (pendingTicks < 4) return current;
            }
        }
        pending = Kind::Unknown;
        pendingTicks = 0;
        current = d;
        return current;
    }

    float soundSpeechProbability() const { return soundP; }

private:
    Decision fromHints (const Guess& hint) const
    {
        const bool soundSure = soundP >= 0 && std::abs (soundP - 0.5f) > 0.25f;
        const Kind soundKind = soundP >= 0.5f ? Kind::Speech : Kind::Music;
        const Decision byHint { hint.kind, hint.fromApp ? Why::App : Why::Title, hint.why };
        // Films and games cannot be told from the sound alone; trust the program or the title.
        if ((hint.kind == Kind::Movie || hint.kind == Kind::Game) && hint.strength >= 0.5f) return byHint;
        if (hint.kind != Kind::Unknown && hint.strength >= 0.75f)
        {
            // A strong hint wins unless the sound clearly says the opposite.
            if (soundSure && soundKind != hint.kind && std::abs (soundP - 0.5f) > 0.4f) return { soundKind, Why::Sound, {} };
            return byHint;
        }
        if (soundSure) return { soundKind, Why::Sound, {} };
        if (hint.kind != Kind::Unknown) return byHint;
        if (soundP >= 0) return { soundKind, Why::Sound, {} };
        return current.why == Why::Default ? Decision {} : current;
    }

    std::wstring lastKey;
    Decision current;
    Kind pending = Kind::Unknown;
    int pendingTicks = 0;
    float soundP = -1;
};
} // namespace rv
