#pragma once
// The "Clip" picture, after the video for "Do I Wanna Know?" (measured frame by frame, see docs/DIWK_clip_analysis.md):
//  - one white line across the whole screen, flat when nothing sounds;
//  - every sound puts a PACKET on it, in the middle: a bell-shaped envelope over a sine (the AM wave of the album cover); how wide,
//    how many waves and how tall depends on what sounded; it is gone in a third of a second;
//  - while only drums play, the drums make the packets (a strong hit a wide one, a weak hit a small ripple left and right); once a
//    melody has come in (guitar, voice), the drums no longer move the line;
//  - a guitar note: a packet over the full width with many waves; a sung syllable: a packet a third to two thirds wide; a held note
//    widens it to the full width;
//  - at the start of every sung line a SWOOP: the whole line bends as one slow wave for half a second (a breath between lines);
//  - a chorus brings coloured voices: around the white packet a red, a blue and a yellow one, each a little wider (yellow outermost);
//  - from the second part of the song on, a line whose words call for a picture folds the wave into a one-line drawing of it (a heart,
//    a phone, a glass...) for a few seconds and unfolds it again; the packets keep running along the drawing.
// Pure C++ (no JUCE): fed once per frame, gives up to four polylines in screen units (x -aspect..aspect, y -1..1 = half heights).
#include "ClipShapes.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace rv::ui
{
struct ClipInput
{
    float dt = 0.016f;
    float aspect = 16.0f / 9.0f;
    // sound
    bool onset = false;            // a new sharp sound started this frame
    float onsetStrength = 0;       // 0..1
    float onsetHigh = 0;           // 0..1 hissy against round
    float voiced = 0;              // 0..1 something with a pitch is sounding (voice or guitar)
    float intensity = 0;           // 0..1 how hard it is sung
    float hold = 0;                // 0..1 a note is held
    float reg = 0.5f;              // 0..1 where the pitch is in its range
    float level = 0;               // 0..1 loudness of everything
    // words
    bool haveWords = false;
    bool lineStarted = false;      // a sung line began this frame
    bool singing = false;          // a sung line is going on now
    bool chorus = false;
    bool backing = false;          // the line is in brackets: backing voices
    float lineLeft = 0;            // seconds until the next line
    float nextLineIn = 99;         // seconds until the next sung line starts (the swoop comes just before it, in the breath)
    int lineShape = -1;            // the drawing this line calls for (index into kClipShapes), -1 none
    bool storyOpen = false;        // drawings are allowed (the song is past its first part)
    int forceShape = -1;           // a drawing asked for regardless of the words (the end of the song)
    // the beat (tracked by the picture): a swell 0..1 once per beat, the time between beats (0 = unknown), how sure the tracker is
    float beatPulse = 0, beatPeriod = 0, beatConf = 0;
    int lineShape2 = -1;           // a second drawing the line calls for (the two make one scene)
    int motif = -1;                // the image of the whole song (its most frequent drawing): it comes back in every chorus
    // the part of the song, as the video tells it (docs/DIWK_clip_full.md): 0 = only the wave (verse 1), 1 = the drawn world (from the
    // first chorus), 2 = full colour (the last chorus: one colour per sung line, black ink on it)
    int act = 0;
    bool stave = false;            // the bridge: the voices apart on four lines
    bool staveUp = false;          // ... standing up: four vertical lines (the second half of the bridge in the video)
    int hue = -1;                  // act 2: which colour this line has
    bool backingNow = false;       // the bracketed backing words are being sung now (blue, left and right)
    bool world = false;            // inside a chorus block: the drawings are one continuous world (see planSong)
    bool burst = false;            // this line starts a chorus block: all four voices at once
    std::string forceLetters;      // capitals to spell now (the end of the song: the artist's initials, as the video ends on "AM")
    float bright = 0.45f;          // 0..1 how bright the sound is (spectral centroid)
};

// Where new sounds start in the music, from the third-octave levels the effect measures (dB, low to high). The voice analyser alone
// finds a syllable only every few seconds in a full mix, which left the line standing still; a rise in dB is the same in a quiet
// song and a loud one, and the threshold follows the song's own recent rises, so every song gets its notes and hits.
struct ClipOnsets
{
    bool onset = false;
    float strength = 0;            // 0..1 against the strongest of the last few seconds
    float high = 0;                // 0..1 how much of the rise was above the bass

    void step (const float* db, int n, float dt, int bassBands = 8)
    {
        onset = false;
        float lo = 0, hi = 0, e = 0;
        for (int i = 0; i < n && i < 32; ++i)
        {
            const float v = std::max (db[i], -90.0f);
            if (have && v > -72.0f) (i < bassBands ? lo : hi) += std::clamp (v - prev[i], 0.0f, 18.0f);
            e += v;
            prev[i] = v;
        }
        const bool fresh = have && e != lastE;   // (the levels may come slower than the frames)
        have = true; lastE = e;
        since += dt;
        if (! fresh) return;
        const float flux = lo + hi;
        const float k = 1.0f - std::exp (-dt * 0.8f);
        mean += (flux - mean) * k;
        var += ((flux - mean) * (flux - mean) - var) * k;
        peak = std::max (flux, peak * std::exp (-dt * 0.3f));
        if (flux > mean + 1.3f * std::sqrt (var) + 1.5f && since > 0.2f)
        {
            onset = true; since = 0;
            strength = std::clamp (flux / std::max (peak, 1.0f), 0.0f, 1.0f);
            high = hi / (flux + 1.0e-6f);
        }
    }

private:
    float prev[32] {};
    bool have = false;
    float lastE = 0, since = 9, mean = 0, var = 0, peak = 0;
};

class ClipLine
{
public:
    static constexpr int N = 600;              // points per line
    static constexpr int kLayers = 4;          // white, red, blue, yellow
    static constexpr int kSlots = kLayers + 2; // and the last drawing, fading out, and a coloured swoop

    struct Output
    {
        int lines = 1;
        float pts[kSlots][N * 2] {};           // the colour layers, then (slot 4) the drawing that is fading out
        float col[kSlots][3] {};
        float alpha[kSlots] {};
        float bg[3] {};                        // the colour behind (act 2), 0 = black
        bool ink = false;                      // draw the lines as black ink on that colour
    };

    void reset()
    {
        packets.clear(); swoopEnv = 0; swoopAge = 9; state = 0; k = 1; shape = -1; sinceMelody = 99; sinceDrawing = 99;
        for (auto& x : lastShown) x = -99.0;
        chorusMix = 0; backingMix = 0; t = 0; std::fill (smooth.begin(), smooth.end(), 0.0f);
    }

    void step (const ClipInput& in, Output& out)
    {
        const float dt = std::clamp (in.dt, 0.0f, 0.1f);
        t += dt;
        sinceMelody += dt; sinceDrawing += dt; swoopAge += dt; sinceAnyPacket += dt;
        const bool melodic = in.voiced > 0.45f;               // a pitch is sounding (the packet of it stays up while it does)
        sincePitch = in.voiced > 0.2f ? 0.0f : sincePitch + dt;
        const bool tune = melodic || sincePitch < 2.0f;        // a pitch was heard just now: a melody plays, not only drums
        if (tune) sinceMelody = 0;
        const bool drumsLead = sinceMelody > 8.0f;            // nothing with a pitch for a while: the drums have the line

        // ---- new packets -------------------------------------------------------------------------------------------------------
        // (every sound that starts makes one: the first version ignored drums under a melody and waited for the voice analyser to be
        // sure of a pitch, and on real music that left the line standing still)
        staveMix += ((in.stave ? 1.0f : 0.0f) - staveMix) * (1.0f - std::exp (-dt * 2.5f));
        staveUpMix += ((in.stave && in.staveUp ? 1.0f : 0.0f) - staveUpMix) * (1.0f - std::exp (-dt * 4.0f));
        backingMix2 += ((in.backingNow ? 1.0f : 0.0f) - backingMix2) * (1.0f - std::exp (-dt * 6.0f));
        nextMask = 0xF;
        if (in.onset && in.backingNow)
        {
            // the backing voice, as in the video: blue, two groups left and right of the middle; the white line stays still
            const float s = std::clamp (in.onsetStrength, 0.0f, 1.0f);
            nextMask = 1 << 2;
            add (0.24f, 0.30f, 8.0f, 0.30f + 0.45f * s); add (0.76f, 0.30f, 8.0f, 0.30f + 0.45f * s);
            nextMask = 0xF;
        }
        else if (in.onset)
        {
            if (staveMix > 0.5f) nextMask = 1 << (staveTurn++ % kLayers);         // the bridge: each sound on one of the four lines
            const float s = std::clamp (in.onsetStrength, 0.0f, 1.0f);
            const bool voice = in.singing || (! in.haveWords && in.intensity > 0.3f && melodic);
            if (voice)
                add (0.5f, 0.32f + 0.33f * std::max (in.intensity, s), 5.0f + 3.0f * in.reg, 0.40f + 0.50f * std::max (in.intensity, s), melodic, 1);   // a syllable
            else if (tune && in.onsetHigh > 0.45f)
                add (0.5f, 0.92f, 8.0f + 9.0f * in.onsetHigh, 0.35f + 0.50f * s, melodic, s > 0.6f ? 2 : 1);                     // a note: guitar, keys
            else if (drumsLead || (s > 0.55f && sinceAnyPacket > 1.5f))           // under a melody a hit moves the line only if nothing else has for a while
                add (0.5f, 0.90f, 11.0f, (drumsLead ? 0.32f : 0.22f) + 0.25f * s, false, 2);                                   // a strong hit: two humps, like the cover
            else if (drumsLead) { add (0.29f, 0.28f, 4.0f, 0.06f + 0.12f * s); add (0.71f, 0.28f, 4.0f, 0.06f + 0.12f * s); }   // a weak one: a ripple (only while the drums lead, as in the video)
        }
        // while the note or the syllable sounds, its packet stays up and follows how loud it is; when it stops, the packet dies away
        if (! packets.empty() && packets.back().sustain)
        {
            auto& p = packets.back();
            if (melodic)
            {
                p.age = std::min (p.age, 0.09f);
                const float want = p.target * (0.55f + 0.45f * std::clamp (std::max (in.intensity, in.level), 0.0f, 1.0f));
                p.amp += (want - p.amp) * (1.0f - std::exp (-dt * 10.0f));
                if (in.hold > 0.5f)                                   // a held note spreads over the screen
                {
                    p.width += (1.0f - p.width) * (1.0f - std::exp (-dt * 2.5f));
                    p.cycles += (13.0f - p.cycles) * (1.0f - std::exp (-dt * 1.5f));
                }
            }
            else p.sustain = false;
        }
        for (auto& p : packets) { p.age += dt; p.phase += dt * 0.6f; }
        packets.erase (std::remove_if (packets.begin(), packets.end(), [] (const Packet& p) { return p.age > 1.4f && ! p.sustain; }), packets.end());

        // ---- the swoop: in the breath just before a sung line ------------------------------------------------------------------
        const bool swoopNow = (in.nextLineIn <= 0.35f && prevNextLine > 0.35f) || (! in.haveWords && in.lineStarted);
        prevNextLine = in.nextLineIn;
        if (swoopNow)
        {
            swoopAge = 0;
            swoopSign = -swoopSign;
            swoopAmp = 0.30f + 0.45f * std::clamp (in.level, 0.0f, 1.0f);
            swoopPhase = (float) std::fmod (t * 1.618, 1.0) * 6.2831853f;
            swoopK = 0.5f + 0.5f * (float) std::fmod (t * 0.618, 1.0);         // half a wave to a whole one across the screen
            // after the first verse the swoop is a coloured arc of its own over the white line (yellow, blue, green, red, as in the video)
            swoopColoured = in.act >= 1;
            static const float sc4[4][3] = { { 1.0f, 0.86f, 0.15f }, { 0.25f, 0.50f, 1.0f }, { 0.20f, 0.85f, 0.30f }, { 1.0f, 0.24f, 0.22f } };
            std::memcpy (swoopCol, sc4[swoopTurn++ % 4], sizeof (swoopCol));
        }
        swoopEnv = swoopAge < 0.08f ? swoopAge / 0.08f : std::exp (-(swoopAge - 0.08f) / 0.16f);

        // ---- the chorus colours ------------------------------------------------------------------------------------------------
        chorusMix += ((in.chorus ? 1.0f : 0.0f) - chorusMix) * (1.0f - std::exp (-dt * (in.chorus ? 3.0f : 1.2f)));
        backingMix += ((in.backing ? 1.0f : 0.0f) - backingMix) * (1.0f - std::exp (-dt * 3.0f));

        // ---- the form under the waves: a slow flow, or a drawing of the words ---------------------------------------------------
        // Everything is timed in beats of the song, so a fast song changes fast and a slow one glides. Nothing stands still: the flow
        // keeps moving with the music, a drawing breathes on the beat and turns a little, and every change is a blend from whatever
        // is on the screen now (the flow, or the last drawing) into the next form.
        const float period = in.beatPeriod > 0.25f ? std::clamp (in.beatPeriod, 0.30f, 1.0f) : 0.55f;
        busy = busy * std::exp (-dt / 2.0f) + (in.onset ? 0.5f : 0.0f);            // about the sounds per second
        energy += (std::clamp (in.level, 0.0f, 1.0f) - energy) * (1.0f - std::exp (-dt * 1.5f));
        const float busyN = std::clamp (busy / 5.0f, 0.0f, 1.0f);
        {
            static const float speed[3] = { 0.125f, 0.19f, 0.31f };            // turns per beat
            for (int h = 0; h < 3; ++h) flowPhase[h] = std::fmod (flowPhase[h] + 6.2831853f * dt / period * speed[h] * (0.7f + 0.6f * busyN), 6.2831853f);
            const float want = 0.0f;   // (a slow flow was tried under the waves; the video's line is flat between sounds, so it is off)
            flowAmp += (want - flowAmp) * (1.0f - std::exp (-dt * 1.2f));
        }
        // the character of the music, as the drawing's hand: dense and bright = angular, sketchy and quick; sparse and dark = smooth,
        // traced slowly like a pen; a steady beat with many sounds (electronic) = drawn twice, mirrored
        bright += (std::clamp (in.bright, 0.0f, 1.0f) - bright) * (1.0f - std::exp (-dt * 0.5f));
        const float angular = std::clamp (0.9f * busyN + 0.4f * bright - 0.3f, 0.0f, 1.0f);
        const float trace = std::clamp (1.0f - 1.4f * busyN, 0.0f, 0.85f);           // how much it is drawn from left to right
        const float sketch = (0.003f + 0.018f * energy * (0.4f + bright)) * (0.5f + angular) * (letters.empty() ? 1.0f : 0.2f);
        echoMix += ((std::max (chorusMix, energy > 0.6f ? 1.0f : 0.0f)) - echoMix) * (1.0f - std::exp (-dt * 1.0f));

        int want = -1, want2 = -1;
        bool evolveNow = false;
        auto fresh = [&] (int s) { return s >= 0 && s < kMaxShapes && t - lastShown[(size_t) s] > 12.0; };   // the same drawing again only after a while
        if (! in.forceLetters.empty())
        {
            if (in.forceLetters != letters || state != 1) { letters = in.forceLetters; want = 0; }
        }
        else if (in.forceShape >= 0) { if (in.forceShape != shape || state != 1 || ! letters.empty()) want = in.forceShape; }
        else if (in.act < 1 || in.stave) {}                                        // verse 1 and the bridge: only the waves, as in the video
        else if (in.lineStarted && in.storyOpen && (state == 0 || k >= 1.0f))
        {
            if (fresh (in.lineShape)) { want = in.lineShape; if (fresh (in.lineShape2) && in.lineShape2 != want) want2 = in.lineShape2; }
            else if (fresh (in.lineShape2)) want = in.lineShape2;
            else if (in.world && in.motif >= 0 && in.motif < kMaxShapes && t - lastShown[(size_t) in.motif] > 6.0) want = in.motif;
            else if (in.world && state == 1) evolveNow = true;                     // the world goes on: the same scene, told another way
        }
        if (want >= 0)
        {
            // a drawing already up does not turn into the next one point by point (that is a scribble half way): it fades out where
            // it is, and the new one is drawn out of the line
            if (state == 1 && shape >= 0) { std::memcpy (ghostPts, base, sizeof (base)); ghost = 1.0f; }
            if (in.forceLetters.empty()) letters.clear();
            shape = want; shape2 = want2;
            // in a chorus the drawing is the WORLD: big, standing on the line, which drops to the floor; in a verse it is a short
            // cutaway, small, on the line where it is (the lips at "pucker up")
            big = in.world || in.act >= 2 || in.forceShape >= 0;
            symmetric = want2 < 0 && big && letters.empty() && in.beatConf > 0.7f && busyN > 0.5f && kClipShapes[shape].name[0] != 'a';
            newVariant (in.aspect); cur = nxt; prev = nxt; evolveK = 1.0f;
            buildShape (in.aspect, angular);
            state = 1; k = 0.0f; transDur = std::clamp ((1.0f + 2.0f * trace) * period, 0.45f, 2.2f); traceNow = std::max (0.35f, trace);
            holdBeats = 4.0f; evolved = false;
            {
                // things that go somewhere walk or roll along the line, as the boots and tyres of the video do
                const std::string nm = kClipShapes[shape].name;
                mover = ! big || ! letters.empty() || shape2 >= 0 ? 0 : nm == "tyre" ? 2 : (nm == "car" || nm == "dancer" || nm == "woman" || nm == "bird" || nm == "legs") ? 1 : 0;
                drift = 0.0f; driftDir = nxt.cx > 0.0f ? -1.0f : 1.0f;
            }
            lastShown[(size_t) shape] = t; if (shape2 >= 0) lastShown[(size_t) shape2] = t;
        }
        if (state == 1 && k >= 1.0f)
        {
            holdBeats -= dt / period;
            const bool forced = in.forceShape >= 0 || ! letters.empty();
            const bool world = big && in.world && ! in.stave;                       // a chorus goes on: the drawing stays and keeps changing
            if (forced) {}
            else if (world)
            {
                if (evolveNow || holdBeats <= 0.0f) { prev = cur; newVariant (in.aspect); evolveK = 0.0f; evolveDur = std::clamp (1.5f * period, 0.5f, 1.2f); holdBeats = 4.0f; }
            }
            else if (! evolved && holdBeats <= 2.0f && ! big)                      // a cutaway: half way it moves a little, then it goes
            {
                prev = cur; newVariant (in.aspect); evolveK = 0.0f; evolveDur = std::clamp (1.5f * period, 0.5f, 1.2f); evolved = true;
            }
            else if (holdBeats <= 0.0f || (big && ! world))                        // back into the line: the drawing unravels
            {
                state = 0; k = 0.0f; transDur = std::clamp ((1.5f + trace) * period, 0.6f, 1.8f); traceNow = std::max (0.35f, trace);
            }
        }        if (state == 1 && mover > 0) drift += driftDir * dt * in.aspect * 0.45f / (5.0f * period);
        if (std::abs (in.aspect - builtAspect) > 0.01f && shape >= 0) buildShape (in.aspect, builtAngular);
        k = std::min (1.0f, k + dt / transDur);
        evolveK = std::min (1.0f, evolveK + dt / evolveDur);
        ghost = std::max (0.0f, ghost - dt / std::max (0.3f, transDur * 0.8f));
        {
            const float e = evolveK * evolveK * (3.0f - 2.0f * evolveK);
            cur.cx = prev.cx + (nxt.cx - prev.cx) * e; cur.cy = prev.cy + (nxt.cy - prev.cy) * e;
            cur.rot = prev.rot + (nxt.rot - prev.rot) * e; cur.scale = prev.scale + (nxt.scale - prev.scale) * e;
        }
        const float ke = k * k * (3.0f - 2.0f * k);
        const float m = state == 1 ? ke : 1.0f - ke;                               // how much of a drawing is on the screen
        if (m <= 0.001f && state == 0) shape = -1;
        // a hand-drawn line "boils": its little wobble is drawn anew on every beat (the old trick of animated drawings)
        if (in.beatPulse > 0.8f && ! boilArmed) { boilArmed = true; boilSeed += 1.618f; }
        if (in.beatPulse < 0.3f) boilArmed = false;

        // ---- the lines ---------------------------------------------------------------------------------------------------------
        const float A = in.aspect;
        static const float cols[kLayers][3] = { { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.24f, 0.22f }, { 0.25f, 0.50f, 1.0f }, { 1.0f, 0.82f, 0.18f } };
        static const float widen[kLayers] = { 1.0f, 1.3f, 1.6f, 2.0f };
        static const float cyc[kLayers] = { 1.0f, 1.12f, 0.9f, 1.25f };
        static const float ampK[kLayers] = { 1.0f, 0.9f, 0.85f, 0.75f };
        if (in.lineStarted && in.burst) burstAge = 0.0f;
        burstAge += dt;
        const float burstEnv = burstAge < 0.1f ? burstAge / 0.1f : std::exp (-(burstAge - 0.1f) / 0.7f);
        const float echoNow = 0.0f;   // (coloured outlines around drawings were tried: the video's drawings are plain white lines)
        const float layersUp = std::max ({ burstEnv * (1.0f - m), staveMix, backingMix2 });
        out.lines = layersUp > 0.02f ? kLayers : 1;
        {
            // the drawing alive: it swells a little on every beat and slowly turns back and forth
            const float conf = std::clamp (in.beatConf, 0.0f, 1.0f);
            const float sc = cur.scale * (1.0f + (0.03f + 0.05f * conf) * in.beatPulse);
            const float th = cur.rot + (letters.empty() ? 0.04f : 0.0f) * std::sin ((float) t * 6.2831853f / (8.0f * period))
                             - (mover == 2 ? drift / std::max (0.1f, cur.scale) : 0.0f);   // a tyre rolls as it goes
            const float cs = std::cos (th) * sc, sn = std::sin (th) * sc;
            static const float fx[3] = { 0.7f, 1.6f, 2.9f };
            const float wh[3] = { 1.0f, 0.5f * (0.4f + busyN), 0.35f * busyN };
            const bool drawn = shape >= 0 && (state == 1 || k < 1.0f);
            for (int i = 0; i < N; ++i)
            {
                const float s = (float) i / (N - 1);
                const float fx0 = -A + 2.0f * A * s;
                float fy0 = 0.0f;
                const float env = std::pow (std::max (0.0f, std::sin (3.14159265f * s)), 0.6f);   // (max: sin(pi) is a hair below 0 in floats, and pow of that is NaN)
                for (int h = 0; h < 3; ++h) fy0 += flowAmp * wh[h] * std::sin (6.2831853f * fx[h] * s + flowPhase[h]) * env;
                float x = fx0, y = fy0;
                if (drawn)
                {
                    const float lx = shapeLocal[(size_t) i * 2], ly = shapeLocal[(size_t) i * 2 + 1], w = leadW[(size_t) i];
                    const float ex = edgeAbs[(size_t) i * 2], ey = edgeAbs[(size_t) i * 2 + 1];
                    float dx = ex + (cur.cx + drift + cs * lx - sn * ly - ex) * w, dy = ey + (cur.cy + sn * lx + cs * ly - ey) * w;
                    // the hand's wobble: slow along the line (a pen, not noise), new on every beat
                    dx += sketch * w * std::sin (i * 0.061f + boilSeed * 3.1f) * std::sin (i * 0.023f + boilSeed);
                    dy += sketch * w * std::sin (i * 0.047f + boilSeed * 2.3f) * std::sin (i * 0.019f + boilSeed * 1.7f);
                    // into the drawing (state 1) or out of it (state 0); traced: the change runs along the line like a pen
                    const float kk = std::clamp (k * (1.0f + traceNow) - traceNow * (state == 1 ? s : 1.0f - s), 0.0f, 1.0f);
                    float ki = kk * kk * (3.0f - 2.0f * kk);
                    if (state == 0) ki = 1.0f - ki;
                    x = fx0 + (dx - fx0) * ki; y = fy0 + (dy - fy0) * ki;
                }
                base[(size_t) i * 2] = x; base[(size_t) i * 2 + 1] = y;
            }
        }        for (int i = 0; i < N; ++i)
        {
            const int a = std::max (0, i - 2), b = std::min (N - 1, i + 2);
            float tx = base[(size_t) b * 2] - base[(size_t) a * 2], ty = base[(size_t) b * 2 + 1] - base[(size_t) a * 2 + 1];
            const float len = std::sqrt (tx * tx + ty * ty) + 1.0e-6f;
            nrm[(size_t) i * 2] = -ty / len; nrm[(size_t) i * 2 + 1] = tx / len;
        }
        for (int L = 0; L < out.lines; ++L)
        {
            float la = L == 0 ? 1.0f : std::max (burstEnv * (1.0f - m), staveMix);
            if (L == 2) la = std::max (la, backingMix2 * (1.0f - m));             // (the backing voice is a wave, never an outline around a drawing)
            static const float staveY[kLayers] = { 0.54f, 0.18f, -0.18f, -0.54f };   // the bridge: white on top, then red, blue, yellow
            const float grow = 0.07f * (float) L * echoNow;                         // loud parts and choruses: the colours are echoes of the drawing
            out.alpha[L] = std::min (1.0f, la);
            std::memcpy (out.col[L], cols[L], sizeof (cols[L]));
            for (int i = 0; i < N; ++i)
            {
                const float s = (float) i / (N - 1);
                float d = 0.0f;
                for (const auto& p : packets)
                {
                    if (! (p.mask & (1 << L))) continue;
                    const float w = std::min (1.0f, p.width * (p.mask == 0xF ? widen[L] : 1.0f)) * 0.5f;   // (a voice on its own line keeps its own width)
                    const float u = (s - p.centre) / std::max (0.02f, w);
                    if (u < -1.6f || u > 1.6f) continue;
                    float env = std::exp (-u * u * 2.6f);
                    if (p.lobes == 2) env = std::exp (-u * u * 1.4f) * (0.25f + 0.75f * std::abs (std::sin (3.14159265f * u)));
                    // comes up in about a tenth of a second and fades in about a quarter: soft, like the video, not a flicker
                    const float a01 = std::min (1.0f, p.age / 0.09f);
                    const float life = p.age < 0.09f ? a01 * a01 * (3.0f - 2.0f * a01) : std::exp (-(p.age - 0.09f) / 0.24f);
                    d += p.amp * ampK[L] * life * env * std::sin (3.14159265f * p.cycles * cyc[L] * u + p.phase * (1.0f + 0.2f * L));
                }
                d = 0.45f * std::tanh (0.7f * d / 0.45f);            // lower, and packets on top of each other never leave the screen
                d *= (1.0f - 0.8f * swoopEnv);
                // on a drawing the waves run along it, smaller, so it can still be seen what it is
                d *= 1.0f - (letters.empty() ? 0.85f : 0.95f) * m;
                if (! swoopColoured) d += swoopSign * swoopAmp * swoopEnv * std::sin (6.2831853f * swoopK * s + swoopPhase) * (1.0f - m) * (L == 0 ? 1.0f : 0.8f);
                // the ends stay put: the line comes in from the left edge and leaves at the right one
                const float edge = std::min (1.0f, std::min (s, 1.0f - s) * 40.0f);
                d *= edge;
                // the line flows from one shape to the next instead of jumping (about 70 ms)
                float& sd = smooth[(size_t) L * N + (size_t) i];
                sd += (d - sd) * (1.0f - std::exp (-dt * 14.0f));
                d = sd;
                const float gw = grow * (state == 1 || m > 0.0f ? leadW[(size_t) i] : 0.0f);
                out.pts[L][(size_t) i * 2] = base[(size_t) i * 2] + (base[(size_t) i * 2] - cur.cx) * gw + nrm[(size_t) i * 2] * d;
                out.pts[L][(size_t) i * 2 + 1] = base[(size_t) i * 2 + 1] + (base[(size_t) i * 2 + 1] - cur.cy) * gw + nrm[(size_t) i * 2 + 1] * d
                                                 + staveY[L] * staveMix * (1.0f - m);
                if (staveUpMix > 0.001f)
                {
                    // standing up: each voice a vertical line, its waves across it
                    static const float staveX[kLayers] = { -0.6f, -0.2f, 0.2f, 0.6f };
                    const float vx = staveX[L] * A + d, vy = -1.0f + 2.0f * s;
                    out.pts[L][(size_t) i * 2] += (vx - out.pts[L][(size_t) i * 2]) * staveUpMix;
                    out.pts[L][(size_t) i * 2 + 1] += (vy - out.pts[L][(size_t) i * 2 + 1]) * staveUpMix;
                }
            }
        }
        if (ghost > 0.01f)
        {
            for (int L = out.lines; L < kLayers; ++L) out.alpha[L] = 0.0f;
            std::memcpy (out.pts[kLayers], ghostPts, sizeof (ghostPts));
            out.col[kLayers][0] = out.col[kLayers][1] = out.col[kLayers][2] = 1.0f;
            out.alpha[kLayers] = 0.8f * ghost * ghost;
            out.lines = kSlots;
        }
        if (swoopColoured && swoopEnv > 0.01f)
        {
            // the coloured swoop: one slow arc across the screen, its own line
            for (int L = out.lines; L < kSlots - 1; ++L) out.alpha[L] = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float s = (float) i / (N - 1);
                out.pts[kSlots - 1][(size_t) i * 2] = -A + 2.0f * A * s;
                out.pts[kSlots - 1][(size_t) i * 2 + 1] = base[(size_t) i * 2 + 1] * (1.0f - m) + swoopSign * swoopAmp * swoopEnv * std::sin (3.14159265f * s) * std::min (1.0f, std::min (s, 1.0f - s) * 30.0f);
            }
            std::memcpy (out.col[kSlots - 1], swoopCol, sizeof (swoopCol));
            out.alpha[kSlots - 1] = std::min (1.0f, swoopEnv * 1.4f);
            out.lines = kSlots;
        }
        {
            // the last chorus: a whole colour behind each sung line, the lines black ink on it (as the video ends its story)
            static const float pal[6][3] = { { 0.95f, 0.85f, 0.25f }, { 0.80f, 0.12f, 0.12f }, { 0.12f, 0.22f, 0.62f }, { 0.12f, 0.62f, 0.20f }, { 0.25f, 0.80f, 0.85f }, { 0.85f, 0.30f, 0.65f } };
            const bool on = in.act >= 2 && in.hue >= 0;
            bgMix += ((on ? 1.0f : 0.0f) - bgMix) * (1.0f - std::exp (-dt * (on ? 10.0f : 3.0f)));
            if (on) for (int c = 0; c < 3; ++c) bgCol[c] += (pal[in.hue % 6][c] - bgCol[c]) * (1.0f - std::exp (-dt * 14.0f));
            for (int c = 0; c < 3; ++c) out.bg[c] = bgCol[c] * bgMix;
            out.ink = bgMix > 0.4f;
        }
    }

    // The drawing a line of words calls for: every word that starts like one of a drawing's words counts (1, or 0.35 for a common
    // word marked "~"); the best drawing wins. -1 = none.
    static int shapeFor (const std::string& lowerUtf8Line) { return shapesFor (lowerUtf8Line).first; }
    // the two best drawings of a line (a line about a girl and a car is a scene with both), -1 where there is none
    static std::pair<int, int> shapesFor (const std::string& lowerUtf8Line)
    {
        std::vector<std::string> words;
        {
            std::string w;
            for (size_t i = 0; i <= lowerUtf8Line.size(); ++i)
            {
                const unsigned char c = i < lowerUtf8Line.size() ? (unsigned char) lowerUtf8Line[i] : ' ';
                const bool letter = c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'';
                if (letter) w += (char) c;
                else if (! w.empty())
                {
                    if (w.size() > 3 && w.compare (w.size() - 3, 3, "in'") == 0) w = w.substr (0, w.size() - 1) + "g";      // callin' = calling
                    words.push_back (w); w.clear();
                }
            }
        }
        // every word votes once, for the drawing with the LONGEST key it starts with ("cards" is a card, not a car)
        const int n = (int) (sizeof (kClipShapes) / sizeof (kClipShapes[0]));
        std::vector<float> score ((size_t) n, 0.0f);
        std::vector<char> used ((size_t) n * 64, 0);         // a key counts once per line
        for (const auto& w : words)
        {
            int bestK = -1, bestKey = -1; size_t bestLen = 0; float bestW = 0;
            for (int k = 0; k < n; ++k)
            {
                const std::string keys = kClipShapes[k].words;
                size_t p = 0; int idx = 0;
                while (p < keys.size())
                {
                    size_t q = keys.find (' ', p);
                    if (q == std::string::npos) q = keys.size();
                    std::string key = keys.substr (p, q - p);
                    p = q + 1;
                    if (key.empty()) continue;
                    float weight = 1.0f;
                    if (key[0] == '~') { weight = 0.35f; key = key.substr (1); }
                    const bool cyr = (unsigned char) key[0] >= 0x80;
                    const size_t extra = cyr ? 10 : (key.size() <= 3 ? 1 : 3);        // endings: "drink-s", "ноч-ью"
                    if (w.size() >= key.size() && w.compare (0, key.size(), key) == 0 && w.size() - key.size() <= extra && key.size() > bestLen)
                        { bestK = k; bestKey = idx; bestLen = key.size(); bestW = weight; }
                    ++idx;
                }
            }
            if (bestK >= 0 && bestKey < 64 && ! used[(size_t) bestK * 64 + (size_t) bestKey])
            {
                used[(size_t) bestK * 64 + (size_t) bestKey] = 1;
                score[(size_t) bestK] += bestW;
            }
        }
        int best = -1, next = -1; float bestScore = 0.30f, nextScore = 0.30f;   // (a common word is enough: a picture for most lines)
        for (int k = 0; k < n; ++k)
        {
            if (score[(size_t) k] > bestScore) { next = best; nextScore = bestScore; bestScore = score[(size_t) k]; best = k; }
            else if (score[(size_t) k] > nextScore) { nextScore = score[(size_t) k]; next = k; }
        }
        return { best, next };
    }
    // The parts of the song, told the way the video tells "Do I Wanna Know?" (docs/DIWK_clip_full.md): until the first chorus only
    // the wave (act 0); from it the drawn world (act 1); from the LAST time the chorus's first line comes back, full colour, one per
    // sung line (act 2); the four lines just before the second chorus are the bridge, the voices apart on four lines.
    struct SongPlan { std::vector<int> act, stave, hue, world, burst; };
    static SongPlan planSong (const std::vector<std::string>& texts, const std::vector<char>& chorus, const std::vector<double>& times)
    {
        const size_t n = texts.size();
        SongPlan p; p.act.assign (n, 0); p.stave.assign (n, 0); p.hue.assign (n, -1); p.world.assign (n, 0); p.burst.assign (n, 0);
        auto key = [] (const std::string& s) { std::string k; for (unsigned char c : s) if (c >= 0x80 || std::isalnum (c)) k += (char) std::tolower (c); return k; };
        auto empty = [&] (size_t q) { return key (texts[q]).empty(); };
        int first = -1;
        for (size_t q = 0; q < n; ++q) if (chorus[q] && ! empty (q)) { first = (int) q; break; }
        std::vector<int> again;
        if (first >= 0) { const auto k0 = key (texts[(size_t) first]); for (size_t q = 0; q < n; ++q) if (key (texts[q]) == k0) again.push_back ((int) q); }
        // the drawn world opens on the chorus's own words, not on a bracketed call-and-answer before it (the video: waves and the blue
        // backing voice until "Crawlin' back to you")
        int act1 = (int) n;
        if (first >= 0)
        {
            act1 = first;
            for (size_t q = (size_t) first; q < n; ++q) if (chorus[q] && ! empty (q) && texts[q].find ('(') != 0) { act1 = (int) q; break; }
        }
        else for (size_t q = 0; q < n; ++q) if (times[q] > 75.0) { act1 = (int) q; break; }
        const int act2 = again.size() >= 2 ? again.back() : (int) n;
        int hue = 0;
        for (size_t q = 0; q < n; ++q)
        {
            p.act[q] = (int) q >= act2 ? 2 : (int) q >= act1 ? 1 : 0;
            if (p.act[q] == 2 && ! empty (q)) p.hue[q] = hue++;
        }
        if (again.size() >= 3)
            for (int q = again[1] - 1, c = 0; q > act1 && c < 4; --q)
                if (! empty ((size_t) q) && ! chorus[(size_t) q]) { p.stave[(size_t) q] = c < 2 ? 2 : 1; ++c; }   // (the last two stand up: 2)
        for (size_t q = 1; q + 1 < n; ++q)                                         // a pause inside the bridge is still the bridge
            if (empty (q) && p.stave[q - 1] && p.stave[q + 1]) p.stave[q] = std::max (p.stave[q - 1], p.stave[q + 1]);
        // a chorus block (chorus lines less than 30 s apart, and what is sung between them) is one continuous drawn world; the verses
        // in between get only short cutaways; the start of each block is the burst of all four voices
        int blockFirst = -1, last = -1;
        for (size_t q = 0; q <= n; ++q)
        {
            const bool c = q < n && chorus[q] && ! empty (q);
            if (c && (last < 0 || times[q] - times[(size_t) last] > 30.0))
            {
                if (blockFirst >= 0) for (int z = blockFirst; z <= last; ++z) p.world[(size_t) z] = 1;
                blockFirst = (int) q;
            }
            if (c) last = (int) q;
            if (q == n && blockFirst >= 0) for (int z = blockFirst; z <= last; ++z) p.world[(size_t) z] = 1;
        }
        for (size_t q = 0; q < n; ++q) { if (p.act[q] == 0) p.world[q] = 0; if (p.act[q] == 2) p.world[q] = 1; if (p.stave[q]) p.world[q] = 0; }
        for (size_t q = 0, prevW = 0; q < n; ++q) if (! empty (q)) { if (p.world[q] && ! prevW) p.burst[q] = 1; prevW = (size_t) p.world[q]; }
        return p;
    }
    static int shapeNamed (const char* name)
    {
        const int n = (int) (sizeof (kClipShapes) / sizeof (kClipShapes[0]));
        for (int k = 0; k < n; ++k) if (std::strcmp (kClipShapes[k].name, name) == 0) return k;
        return -1;
    }
    int shownShape() const { return state == 1 && k > 0.5f ? shape : -1; }

    // a drawing as points in its 100 x 100 box (y down); "@name" are made here (the preview script draws them the same way)
    static std::vector<float> pathPoints (const char* path)
    {
        std::vector<float> pts;
        const std::string p = path;
        if (p == "@sun")
        {
            for (int i = 0; i <= 96; ++i) { const double a = 3.14159265 + i / 96.0 * 6.2831853; pts.push_back ((float) (50 + 22 * std::cos (a))); pts.push_back ((float) (50 + 22 * std::sin (a))); }
            for (int i = 0; i <= 24; ++i) { const double a = 3.14159265 + i / 24.0 * 6.2831853, r = i % 2 == 0 ? 30 : 45; pts.push_back ((float) (50 + r * std::cos (a))); pts.push_back ((float) (50 + r * std::sin (a))); }
            return pts;
        }
        if (p == "@flower")
        {
            const float head[] = { 50, 98, 50, 58 };
            pts.insert (pts.end(), head, head + 4);
            for (int i = 0; i <= 240; ++i) { const double tt = i / 240.0 * 6.2831853, r = 30 * std::abs (std::sin (2.5 * tt)) + 4; pts.push_back ((float) (50 + r * std::cos (tt - 1.5707963))); pts.push_back ((float) (38 + r * std::sin (tt - 1.5707963))); }
            const float tail[] = { 50, 58, 50, 70, 70, 60 };
            pts.insert (pts.end(), tail, tail + 6);
            return pts;
        }
        if (p == "@spiral")
        {
            for (int i = 0; i <= 240; ++i) { const double tt = i / 240.0 * 6 * 3.14159265, r = 2 + 42.0 * i / 240; pts.push_back ((float) (50 + r * std::cos (tt + 3.14159265))); pts.push_back ((float) (50 + r * std::sin (tt + 3.14159265))); }
            return pts;
        }
        std::vector<std::string> tok;
        {
            std::string w;
            for (char c : p) { if (c == ' ' || c == ',') { if (! w.empty()) tok.push_back (w); w.clear(); } else w += c; }
            if (! w.empty()) tok.push_back (w);
        }
        char cmd = 'M';
        float cx = 0, cy = 0;
        size_t i = 0;
        auto num = [&] { return i < tok.size() ? (float) std::atof (tok[i++].c_str()) : 0.0f; };
        while (i < tok.size())
        {
            if (tok[i] == "M" || tok[i] == "L" || tok[i] == "C" || tok[i] == "Q") { cmd = tok[i][0]; ++i; continue; }
            if (cmd == 'M' || cmd == 'L') { cx = num(); cy = num(); pts.push_back (cx); pts.push_back (cy); }
            else if (cmd == 'C')
            {
                const float x1 = num(), y1 = num(), x2 = num(), y2 = num(), x3 = num(), y3 = num();
                for (int k = 1; k <= 16; ++k)
                {
                    const float tt = k / 16.0f, u = 1 - tt;
                    pts.push_back (u * u * u * cx + 3 * u * u * tt * x1 + 3 * u * tt * tt * x2 + tt * tt * tt * x3);
                    pts.push_back (u * u * u * cy + 3 * u * u * tt * y1 + 3 * u * tt * tt * y2 + tt * tt * tt * y3);
                }
                cx = x3; cy = y3;
            }
            else if (cmd == 'Q')
            {
                const float x1 = num(), y1 = num(), x2 = num(), y2 = num();
                for (int k = 1; k <= 12; ++k)
                {
                    const float tt = k / 12.0f, u = 1 - tt;
                    pts.push_back (u * u * cx + 2 * u * tt * x1 + tt * tt * x2);
                    pts.push_back (u * u * cy + 2 * u * tt * y1 + tt * tt * y2);
                }
                cx = x2; cy = y2;
            }
            else ++i;
        }
        return pts;
    }

private:
    struct Packet { float centre, width, cycles, amp, age, phase; bool sustain; int lobes; float target; int mask = 0xF; };
    void add (float centre, float width, float cycles, float amp, bool sustain = false, int lobes = 1)
    {
        for (auto& p : packets) p.sustain = false;          // only the newest one is held up
        sinceAnyPacket = 0.0f;
        packets.push_back ({ centre, width, cycles, amp, 0.0f, (float) std::fmod (t * 0.6, 6.2831853), sustain, lobes, amp, nextMask });   // (the phase runs on with the clock: the same sound again looks the same, it does not flip over)
        if (packets.size() > 6) packets.erase (packets.begin());
    }

    // A new look for the next drawing: its size, place, tilt and whether it is mirrored (so the same word never comes back the same)
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }
    void newVariant (float aspect)
    {
        float sc = big ? 0.55f + 0.20f * rnd() : 0.24f + 0.08f * rnd();         // big: it stands on the line and fills the screen above it
        varMirror = rnd() < 0.5f && kClipShapes[shape].name[0] != 'a';          // (letters stay readable)
        if (shape2 >= 0 || symmetric) sc = std::min (sc, 0.95f * aspect / 1.65f);   // a scene of two must fit the screen
        const float halfW = shape2 >= 0 || symmetric ? 1.6f * sc : sc;
        const float room = std::max (0.0f, aspect - halfW - 0.08f);
        if (! letters.empty())                                                 // a word: straight, in the middle, as big as fits
        {
            varMirror = false;
            nxt.scale = std::min (0.5f, 1.75f * aspect / (2.25f * (float) letters.size()));
            nxt.rot = 0; nxt.cx = 0; nxt.cy = 0;
            return;
        }
        nxt.scale = sc;
        nxt.rot = (rnd() - 0.5f) * 0.12f;
        nxt.cx = (rnd() - 0.5f) * 2.0f * std::min (room, 0.45f * aspect);
        nxt.cy = big ? kGround + nxt.scale : 0.0f;                             // its foot on the line, which has dropped to the floor; a cutaway sits on the line
    }
    // the drawing: in from the left edge, the drawing (around its own centre, kept in shapeLocal), out to the right edge, spread
    // evenly along its length over the N points; leadW says how much of a point belongs to the drawing (0 at the screen edges)
    void buildShape (float aspect, float angular)
    {
        builtAspect = aspect; builtAngular = angular;
        // the scene: one drawing, two drawings side by side (two things in the words), or one drawing and its mirror image
        struct Item { const char* path; bool mirror; float dx, scale; };
        std::vector<Item> items;
        const char* p1 = kClipShapes[shape].path;
        // (built at size 1 around 0; the size, tilt and place are applied while drawing, so they can move smoothly)
        if (! letters.empty())
        {
            for (size_t j = 0; j < letters.size(); ++j)
                if (letters[j] >= 'A' && letters[j] <= 'Z') items.push_back ({ kClipLetters[letters[j] - 'A'], false, ((float) j - 0.5f * (float) (letters.size() - 1)) * 2.25f, 1.0f });
        }
        else if (shape2 >= 0) items = { { p1, varMirror, -0.78f, 0.75f }, { kClipShapes[shape2].path, false, 0.78f, 0.75f } };
        else if (symmetric) items = { { p1, true, -0.72f, 0.7f }, { p1, false, 0.72f, 0.7f } };
        else items = { { p1, varMirror, 0.0f, 1.0f } };
        std::vector<float> draw;
        float prevEndX = 50.0f, prevEndY = 95.0f;
        for (const auto& it : items)
        {
            auto raw = pathPoints (it.path);
            // the hand: angular music keeps only every few points of the curves (facets), calm music rounds the corners off
            const size_t keep = letters.empty() ? 1 + (size_t) std::lround (angular * 7.0f) : 1;   // (letters stay letters)
            if (keep > 1 && raw.size() > 8)
            {
                std::vector<float> f;
                for (size_t q = 0; q + 1 < raw.size(); q += 2 * keep) { f.push_back (raw[q]); f.push_back (raw[q + 1]); }
                f.push_back (raw[raw.size() - 2]); f.push_back (raw.back());
                raw.swap (f);
            }
            for (int pass = 0; pass < (angular < 0.25f && letters.empty() ? 2 : 0); ++pass)       // Chaikin: each corner cut into two
            {
                std::vector<float> f { raw[0], raw[1] };
                for (size_t q = 0; q + 3 < raw.size(); q += 2)
                {
                    f.push_back (0.75f * raw[q] + 0.25f * raw[q + 2]); f.push_back (0.75f * raw[q + 1] + 0.25f * raw[q + 3]);
                    f.push_back (0.25f * raw[q] + 0.75f * raw[q + 2]); f.push_back (0.25f * raw[q + 1] + 0.75f * raw[q + 3]);
                }
                f.push_back (raw[raw.size() - 2]); f.push_back (raw.back());
                raw.swap (f);
            }
            if (! letters.empty() && ! draw.empty() && raw.size() >= 2)
            {
                // letters are joined like handwriting: out of a letter that ends high to the right, into the gap, down to the line
                // under the letters; along it; up the gap into a letter that starts high (so no join cuts across a letter)
                auto put = [&] (const Item& at, float bx, float by)
                {
                    draw.push_back (nxt.cx + nxt.scale * ((bx - 50.0f) / 50.0f * at.scale + at.dx));
                    draw.push_back (nxt.cy - nxt.scale * (by - 50.0f) / 50.0f * at.scale);
                };
                const Item& pv = items[(size_t) (&it - items.data()) - 1];
                if (prevEndY < 56.0f) put (pv, 98.0f, prevEndY);
                put (pv, prevEndY < 56.0f ? 98.0f : prevEndX, 106.0f);
                if (raw[1] < 50.0f) { put (it, 2.0f, 106.0f); put (it, 2.0f, raw[1]); }
                else put (it, raw[0], 106.0f);
            }            for (size_t q = 0; q + 1 < raw.size(); q += 2)
            {
                const float lx = (raw[q] - 50.0f) / 50.0f * it.scale;
                draw.push_back (nxt.cx + nxt.scale * ((it.mirror ? -lx : lx) + it.dx)); draw.push_back (nxt.cy - nxt.scale * (raw[q + 1] - 50.0f) / 50.0f * it.scale);
            }
            if (raw.size() >= 2) { prevEndX = raw[raw.size() - 2]; prevEndY = raw.back(); }
        }
        std::vector<float> poly;
        const float g = letters.empty() && big ? kGround : 0.0f;
        poly.push_back (-aspect); poly.push_back (g);
        // the line lies on the floor up to under the drawing and rises straight into it (not across the screen from a corner)
        const bool floor = draw.size() >= 2 && big && letters.empty();
        if (floor) { poly.push_back (draw[0]); poly.push_back (g); }
        poly.insert (poly.end(), draw.begin(), draw.end());
        if (floor) { poly.push_back (draw[draw.size() - 2]); poly.push_back (g); }
        poly.push_back (aspect); poly.push_back (g);        const size_t P = poly.size() / 2;
        std::vector<float> acc (P, 0.0f);
        for (size_t q = 1; q < P; ++q) acc[q] = acc[q - 1] + std::hypot (poly[q * 2] - poly[q * 2 - 2], poly[q * 2 + 1] - poly[q * 2 - 1]);
        const float total = std::max (1.0e-4f, acc[P - 1]), leadIn = acc[1], leadOut = total - acc[P - 2];
        size_t seg = 1;
        for (int i = 0; i < N; ++i)
        {
            const float want = total * (float) i / (N - 1);
            while (seg < P - 1 && acc[seg] < want) ++seg;
            const float a = acc[seg - 1], b = acc[seg];
            const float f = b > a ? (want - a) / (b - a) : 0.0f;
            const float px = poly[(seg - 1) * 2] + (poly[seg * 2] - poly[(seg - 1) * 2]) * f;
            const float py = poly[(seg - 1) * 2 + 1] + (poly[seg * 2 + 1] - poly[(seg - 1) * 2 + 1]) * f;
            edgeAbs[(size_t) i * 2] = px; edgeAbs[(size_t) i * 2 + 1] = py;
            shapeLocal[(size_t) i * 2] = (px - nxt.cx) / nxt.scale; shapeLocal[(size_t) i * 2 + 1] = (py - nxt.cy) / nxt.scale;
            leadW[(size_t) i] = want < leadIn ? want / std::max (1.0e-4f, leadIn) : want > total - leadOut ? (total - want) / std::max (1.0e-4f, leadOut) : 1.0f;
        }
    }
    std::vector<Packet> packets;
    float base[N * 2] {}, nrm[N * 2] {};
    float swoopEnv = 0, swoopAge = 9, swoopAmp = 0.4f, swoopSign = 1, swoopPhase = 0, swoopK = 1, prevNextLine = 99;
    static constexpr int kMaxShapes = 64;
    float fromPts[N * 2] {}, shapeLocal[N * 2] {}, leadW[N] {};
    float k = 1.0f, transDur = 1.0f, holdBeats = 0, builtAspect = 0;
    float busy = 0, energy = 0, flowAmp = 0, flowPhase[3] { 0.0f, 2.1f, 4.2f };
    struct Look { float cx = 0, cy = 0, rot = 0, scale = 0.8f; };
    Look cur, prev, nxt;                      // where the drawing is, where it came from, where it is going (it moves there smoothly)
    float evolveK = 1.0f, evolveDur = 1.0f, ghost = 0;
    float edgeAbs[N * 2] {}, ghostPts[N * 2] {};
    bool varMirror = false;
    uint32_t rng = 12345u;
    std::vector<double> lastShown = std::vector<double> ((size_t) kMaxShapes, -99.0);
    int shape = -1, state = 0;                 // state 0: the flow, 1: a drawing
    std::string letters;                       // spelling these instead of a drawing
    static constexpr float kGround = -0.55f;   // where the line lies while a drawing stands on it
    bool big = true; float burstAge = 99.0f, sinceAnyPacket = 99.0f, staveUpMix = 0;
    int mover = 0, staveTurn = 0, nextMask = 0xF; float drift = 0, driftDir = 1, staveMix = 0, backingMix2 = 0;
    float bgMix = 0, bgCol[3] {}, swoopCol[3] { 1.0f, 0.82f, 0.18f }; int swoopTurn = 0; bool swoopColoured = false;
    int shape2 = -1; bool symmetric = false, evolved = false, boilArmed = false;
    float bright = 0.4f, echoMix = 0, traceNow = 0, boilSeed = 0, builtAngular = 0;
    std::vector<float> smooth = std::vector<float> ((size_t) kLayers * N, 0.0f);
    float sinceMelody = 99, sincePitch = 99, sinceDrawing = 99, chorusMix = 0, backingMix = 0;
    double t = 0;
};
} // namespace rv::ui
