#pragma once
// The "Visual" view of the main window: what the music looks like, in the spirit of the old Winamp and
// Windows Media Player visualisers but drawn in a modern way (soft light, smooth motion, no flicker).
//
// Five styles, all driven by the same few numbers taken from the 26 third-octave bands the effect measures:
//   bass / mid / treble (smoothed differently, so bass moves slowly and treble sparkles), the overall level, the
//   spectral centroid (dark and warm .. bright and cold) and a beat pulse (adaptive threshold on the bass).
// The colours follow the character of what is playing: the hue of the mode (music, speech, movies, games),
// shifted by how bright the sound is. Speech moves calmly, games hit harder. Silence draws a quiet breathing
// picture instead of a dead screen.
//
// advance (dt) does all the work and paint() only draws, so a picture can be made without a window (--vizshot).
#include "../dsp/Shared.h"
#include "NebulaGl.h"
#include "Lyrics.h"
#include "LyricAligner.h"
#include "ClipLine.h"
#include <map>
#include "Stall.h"
#include "ScreenPalette.h"
#include "TrackLearner.h"
#include "VoiceCapture.h"
#include "Theme.h"
#include <algorithm>
#include <memory>
#include <thread>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace rv::ui
{
enum class VizStyle { Song = 0, Vortex, Voice, Wave, Crystal, Neon, Flow, Pulse, Aurora, Stars, Horizon };
constexpr int kVizStyles = 11;
inline bool onTheGpu (VizStyle s) { return s == VizStyle::Neon || s == VizStyle::Vortex || s == VizStyle::Crystal || s == VizStyle::Song || s == VizStyle::Voice || s == VizStyle::Wave; }

inline juce::String vizStyleName (VizStyle s)
{
    switch (s)
    {
        case VizStyle::Neon: return L ("Неон", "Neon");
        case VizStyle::Vortex: return L ("Вихрь", "Vortex");
        case VizStyle::Song: return L ("Песня", "Song");
        case VizStyle::Voice: return L ("Голос", "Voice");
        case VizStyle::Wave: return L ("Клип", "Clip");
        case VizStyle::Crystal: return L ("Кристалл", "Crystal");
        case VizStyle::Flow: return L ("Поток", "Flow");
        case VizStyle::Pulse: return L ("Пульс", "Pulse");
        case VizStyle::Aurora: return L ("Сияние", "Aurora");
        case VizStyle::Stars: return L ("Звёзды", "Stars");
        default: return L ("Горизонт", "Horizon");
    }
}

// What the user chose, kept between runs (%APPDATA%\Rovno\visual.txt: "v2 visual style auto follow theater").
struct VizPrefs
{
    bool visual = false;                 // the main window shows the visual instead of the spectrum
    VizStyle style = VizStyle::Song;     // the main picture: the song itself, with its words
    bool autoCycle = false;              // change the style now and then
    bool followVideo = true;             // colours of the picture follow the colours of the video on the screen
    bool theater = false;                // the compact layout: a big picture and a bar of icons
    double lyricLead = 0.0;              // the words come this many seconds before they are sung (the slider in "Song"), 0..2

    static constexpr double kMaxLead = 2.0;
    static juce::File file() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("visual.txt"); }
    static VizPrefs& get()
    {
        static VizPrefs p = load();
        return p;
    }
    void save() const
    {
        const auto f = file();
        f.getParentDirectory().createDirectory();
        f.replaceWithText ("v2 " + juce::String (visual ? 1 : 0) + " " + juce::String ((int) style) + " " + juce::String (autoCycle ? 1 : 0)
                           + " " + juce::String (followVideo ? 1 : 0) + " " + juce::String (theater ? 1 : 0) + " " + juce::String (lyricLead, 2));
    }
    static VizPrefs load()
    {
        VizPrefs p;
        const auto t = juce::StringArray::fromTokens (file().loadFileAsString().trim(), " ", "");
        if (t.size() >= 6 && t[0] == "v2")
        {
            p.visual = t[1].getIntValue() != 0;
            p.style = (VizStyle) juce::jlimit (0, kVizStyles - 1, t[2].getIntValue());
            p.autoCycle = t[3].getIntValue() != 0;
            p.followVideo = t[4].getIntValue() != 0;
            p.theater = t[5].getIntValue() != 0;
            if (t.size() >= 7) p.lyricLead = juce::jlimit (0.0, kMaxLead, t[6].getDoubleValue());
        }
        else if (t.size() >= 3)
            p.visual = t[0].getIntValue() != 0;   // an older file: only "visual or spectrum" is kept; Vortex and no automatic change are the defaults now
        return p;
    }
};
class Visualizer : public juce::Component, private FrameTimer
{
public:
    explicit Visualizer (bool fullscreenView = false) : FrameTimer (this), full (fullscreenView)
    {
        setWantsKeyboardFocus (full);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (60);
        style = VizPrefs::get().style;
        autoCycle = VizPrefs::get().autoCycle;
        follow = VizPrefs::get().followVideo;
        bg = col::bg;
    }

    ~Visualizer() override
    {
        ScreenPalette::get().want (this, false);
        gl.detach();
    }

    // Where the newest short-window spectrum comes from (the effect, straight, every frame). Returns false when not available.
    std::function<bool (std::array<float, (size_t) kBands>&)> pull;
    std::function<void (bool)> onFollow;                  // the colours-of-the-video button was pressed
    bool followsVideo() const { return follow; }
    void setFollow (bool on) { follow = on; }

    std::function<void()> onExpand, onClose;            // the corner button (open full screen / close it)
    std::function<void (VizStyle, bool autoOn)> onChoice;   // the user changed the style or the automatic change

    VizStyle currentStyle() const { return style; }
    void previewControls() { hoverTarget = true; hover = 1.0f; layoutControls(); }   // for pictures
    void setAuto (bool on) { autoCycle = on; sinceSwitch = 0; }

    // Fresh measurements (about 30 times a second): third-octave levels in dB, the mode in use, and whether
    // the effect is running.
    void feed (const std::array<float, (size_t) kBands>& dbBands, Kind k, bool isLive)
    {
        bandsDb = dbBands;
        kind = k;
        live = isLive;
    }

    // What is playing and where in the song it is (Windows "Now playing"), for the words of the "Song" picture. Called every tick.
    void setTrack (const juce::String& artist, const juce::String& title, const juce::String& album, bool hasTimeline,
                   double posSec, double durSec, double stampMs, bool playing)
    {
        trackPos = posSec; trackDur = durSec; trackStamp = stampMs; trackPlaying = playing; trackTimeline = hasTimeline;
        trackArtist = artist; trackTitle = title;
        const juce::String key = (artist + "|" + title).toLowerCase();
        if (key != songKey)
        {
            songKey = key;
            song = nullptr;
            lyricsState = 0;
            lyricScroll = 0.0f;
            // a new song: judge it as itself, not as a continuation of the last one
            gaps.clear(); lastBeatT = -1.0f; confS = 0.0f; period = 0.5f;
            eFast = eSlow = level; moodE = level; sinceSection = 0.0f; sectionTarget = sectionShift;
            densP = -1.0f; ageP = 0.0f; chorusMix = 0.0f;
            loadOffset();
            trackSince = juce::Time::getMillisecondCounterHiRes();
            ++fetchGen;
            lyricsTries = 0; lyricsRetryAt = 0.0;
            autoShift = 0.0; savedShift = 0.0; aligner.reset ({}, 0.0);
            clipLine.reset(); lineShapes.clear(); chorusPassed = false; clipLineSeen = -1;
        }
        if (lyricsState == 2 && song == nullptr && lyricsRetryAt > 0.0 && juce::Time::getMillisecondCounterHiRes() >= lyricsRetryAt)
        {
            lyricsRetryAt = 0.0;
            lyricsState = 0;
        }
        if ((style == VizStyle::Song || style == VizStyle::Wave) && title.isNotEmpty() && lyricsState == 0) startLyricsFetch (artist, title, album, durSec);
    }

    // For pictures and tests: the words of a song and a fixed place in it (no player involved).
    void setSongForTest (std::shared_ptr<LyricsData> d, const juce::String& artist, const juce::String& title, double posSec)
    {
        song = (d != nullptr && d->found) ? d : nullptr;
        trackArtist = artist; trackTitle = title;
        trackTimeline = true; trackPlaying = false; trackPos = posSec - 0.25; trackStamp = 0;
        songKey = (artist + "|" + title).toLowerCase();
        lyricsState = 2;
        lyricScroll = (float) juce::jmax (0, lineAt (positionNow()));
    }

    // A made-up piece of music for pictures and tests: a kick, a bass line, hats and a drifting melody.
    static void fakeBands (double t, std::array<float, (size_t) kBands>& out)
    {
        const double beatPos = std::fmod (t * 2.0833, 1.0);          // 125 bpm
        const float kick = (float) std::exp (-beatPos * 7.0);
        const float hat = (float) std::exp (-std::fmod (t * 4.1666 + 0.5, 1.0) * 9.0);
        const float swell = 0.5f + 0.5f * (float) std::sin (t * 0.35);
        for (int i = 0; i < kBands; ++i)
        {
            const float x = (float) i;
            float db = -56.0f + 17.0f * std::exp (-std::pow ((x - 6.0f) / 7.0f, 2.0f));
            if (i < 8) db += 20.0f * kick;
            db += 9.0f * swell * std::exp (-std::pow ((x - (11.0f + 6.0f * (float) std::sin (t * 0.31))) / 3.0f, 2.0f));
            if (i >= 17) db += 14.0f * hat - 3.0f;
            db += 2.5f * (float) std::sin (t * 1.7 + x * 0.9);
            out[(size_t) i] = db;
        }
    }

    // ---- the picture's own clock -------------------------------------------------------------
    void advance (float dt)
    {
        dt = juce::jlimit (0.001f, 0.05f, dt);
        t += dt;
        measure (dt);
        if (width() < 8 || height() < 8) return;
        switch (style)
        {
            case VizStyle::Neon:
            case VizStyle::Song:
            case VizStyle::Voice:
            case VizStyle::Wave:
            case VizStyle::Crystal:
            case VizStyle::Vortex: stepNeon (dt); break;
            case VizStyle::Flow: stepFlow (dt); break;
            case VizStyle::Stars: stepStars (dt); break;
            case VizStyle::Horizon: stepHorizon (dt); break;
            default: break;
        }
        // automatic change of style, with a short dip to black in between
        sinceSwitch += dt;
        if (autoCycle && sinceSwitch > 45.0f && pending < 0) requestStyle ((VizStyle) (((int) style + 1) % kVizStyles));
        if (pending >= 0)
        {
            veil = std::min (1.0f, veil + dt / 0.35f);
            if (veil >= 1.0f) { applyStyle ((VizStyle) pending); pending = -1; }
        }
        else if (veil > 0) veil = std::max (0.0f, veil - dt / 0.45f);
        hover += ((hoverTarget ? 1.0f : 0.0f) - hover) * rate (dt, 10.0f);
        nameFlash = std::max (0.0f, nameFlash - dt);
        if (full && hoverTarget && juce::Time::getMillisecondCounter() - lastMove > 3000) { hoverTarget = false; setMouseCursor (juce::MouseCursor::NoCursor); }
    }

    void setStyleNow (VizStyle s, bool notify = false)
    {
        applyStyle (s);
        pending = -1;
        veil = 0;
        if (notify && onChoice) onChoice (style, autoCycle);
    }

    // ---- drawing ---------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const double paintStart = juce::Time::getMillisecondCounterHiRes();
        struct Probe { Visualizer& v; double t0; ~Probe() { v.noteFrame (t0); } } probe { *this, paintStart };
        const auto area = getLocalBounds().toFloat();
        if (area.getWidth() < 8 || area.getHeight() < 8) return;
        juce::Graphics::ScopedSaveState keep (g);
        const bool onGpu = onTheGpu (style) && gl.isAttached() && ! nebula->failed;
        if (! full)
        {
            juce::Path clip;
            clip.addRoundedRectangle (area, 10.0f);
            g.reduceClipRegion (clip);
        }
        if (! onGpu)   // on the graphics card the picture is already there; only the buttons go on top
        {
            g.setColour (bg.interpolatedWith (juce::Colours::black, 0.35f));
            g.fillRect (area);
        }
        switch (style)
        {
            case VizStyle::Neon:
            case VizStyle::Song:
            case VizStyle::Voice:
            case VizStyle::Wave:
            case VizStyle::Crystal:
            case VizStyle::Vortex: if (! onGpu) paintAurora (g, area); break;   // the same colours if there is no GPU picture
            case VizStyle::Flow: paintFlow (g, area); break;
            case VizStyle::Pulse: paintPulse (g, area); break;
            case VizStyle::Aurora: paintAurora (g, area); break;
            case VizStyle::Stars: paintStars (g, area); break;
            default: paintHorizon (g, area); break;
        }
        if (style == VizStyle::Song) { StallProbe sp ("Visualizer::paintLyrics", 12.0); paintLyrics (g, area); }
        if (offsetFlash > 0.0f && style == VizStyle::Song)
        {
            offsetFlash = std::max (0.0f, offsetFlash - 1.0f / 60.0f);
            g.setFont (font (full ? 18.0f : 13.0f, 600));
            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, offsetFlash)));
            g.drawText (L ("текст ", "words ") + juce::String (lyricOffset >= 0 ? "+" : "") + juce::String (lyricOffset, 2) + L (" с", " s"), area.withHeight (full ? 54.0f : 30.0f), juce::Justification::centred);
        }
        if (veil > 0.001f) { g.setColour (bg.interpolatedWith (juce::Colours::black, 0.35f).withAlpha (veil)); g.fillRect (area); }
        paintControls (g, area);
    }

    void resized() override { layoutControls(); }

    // ---- mouse and keys ----------------------------------------------------------------------------
    void mouseEnter (const juce::MouseEvent& e) override { mouse = e.position; poke(); }
    void mouseMove (const juce::MouseEvent& e) override { mouse = e.position; poke(); }
    void mouseExit (const juce::MouseEvent&) override { hoverTarget = false; mouse = { -1.0f, -1.0f }; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (leadShown() && leadR.contains (e.position)) { leadDrag = true; setLeadFrom (e.position.x); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (leadDrag) { mouse = e.position; setLeadFrom (e.position.x); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (leadDrag) { leadDrag = false; VizPrefs::get().save(); return; }
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const auto p = e.position;
        if (cornerR.contains (p)) { if (full) { if (onClose) onClose(); } else if (onExpand) onExpand(); return; }
        if (prevR.contains (p)) { step (-1); return; }
        if (nextR.contains (p)) { step (+1); return; }
        if (autoR.contains (p)) { autoCycle = ! autoCycle; sinceSwitch = 0; if (onChoice) onChoice (style, autoCycle); return; }
        if (paletteR.contains (p)) { toggleFollow(); return; }
        // a click on the picture itself does nothing: the style changes only with the buttons or the keys
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (const auto& r : { cornerR, prevR, nextR, autoR, paletteR, leadR })
            if (r.contains (e.position)) return;
        if (full) { if (onClose) onClose(); } else if (onExpand) onExpand();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        if (k == juce::KeyPress::rightKey || k == juce::KeyPress::spaceKey || k == juce::KeyPress::downKey) { step (+1); return true; }
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::upKey) { step (-1); return true; }
        {
            const juce::juce_wchar tc = k.getTextCharacter();
            double d = 0;
            if (tc == '[') d = -0.25; else if (tc == ']') d = 0.25; else if (tc == '{') d = -1.0; else if (tc == '}') d = 1.0;
            if (d != 0 && songKey.isNotEmpty()) { lyricOffset += d; offsetFlash = 2.0f; saveOffset(); return true; }
        }
        const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (ch == 'a') { autoCycle = ! autoCycle; sinceSwitch = 0; if (onChoice) onChoice (style, autoCycle); return true; }
        if (ch == 'p' || ch == 'c') { toggleFollow(); return true; }
        const int digit = k.getTextCharacter() - '1';
        if (digit >= 0 && digit < kVizStyles) { requestStyle ((VizStyle) digit); if (onChoice) onChoice ((VizStyle) digit, autoCycle); return true; }
        return false;
    }
private:
    // ---- plumbing ------------------------------------------------------------------------------
    void timerCallback() override
    {
        StallProbe whole ("Visualizer::timerCallback");
        if (! isShowing())
        {
            ScreenPalette::get().want (this, false);
            syncGpu();
            return;
        }
        if (pull)
        {
            std::array<float, (size_t) kBands> fresh;
            if (pull (fresh)) bandsDb = fresh;
        }
        ScreenPalette::get().want (this, follow);
        if (style == VizStyle::Song && ++heardTick % 60 == 0) takeHeard();
        advance (dt());
        syncGpu();
        repaint();
    }
    // The Neon picture runs on the graphics card, attached only while it is on screen.
    // (never inside the frame callback itself: attaching changes the lists that callback is walking)
    void syncGpu()
    {
        const bool want = onTheGpu (style) && ! nebula->failed && isShowing();
        if (want == gl.isAttached() || gpuPending) return;
        gpuPending = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Visualizer> (this)] { if (safe != nullptr) safe->applyGpu(); });
    }
    void applyGpu()
    {
        gpuPending = false;
        const bool want = onTheGpu (style) && ! nebula->failed && isShowing();
        if (want && ! gl.isAttached())
        {
            gl.setRenderer (nebula.get());
            gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
            gl.setComponentPaintingEnabled (true);
            gl.setContinuousRepainting (true);
            gl.attachTo (*this);
        }
        else if (! want && gl.isAttached())
            gl.detach();
    }
    void stepNeon (float dt)
    {
        neonPhase += dt * (0.22f + 1.5f * bass + 0.5f * beat) * motion;
        neonRot += dt * (0.04f + 0.14f * mid + 0.08f * beat) * motion;
        NebulaFrame f;
        f.mode = (style == VizStyle::Vortex || style == VizStyle::Song) ? 1 : style == VizStyle::Voice ? 3 : style == VizStyle::Wave ? 4 : style == VizStyle::Crystal ? 2 : 0;
        f.dt = dt;
        f.time = t; f.phase = neonPhase; f.rot = neonRot;
        f.bass = bass; f.mid = mid; f.treble = treble; f.beat = beat; f.level = level; f.idle = idle;
        auto rgb = [] (float* o, juce::Colour c) { o[0] = c.getFloatRed(); o[1] = c.getFloatGreen(); o[2] = c.getFloatBlue(); };
        rgb (f.colA, pa); rgb (f.colB, pb); rgb (f.colC, pc);
        for (int i = 0; i < kBands; ++i) f.bands[i] = view[(size_t) i];
        if (style == VizStyle::Voice) stepVoice (dt, f);
        if (style == VizStyle::Wave) stepClip (dt, f);
        if (style == VizStyle::Song) stepSong (dt, f);
        if (style == VizStyle::Crystal)
        {
            // A fractal re-arranges itself completely when its folds move a little, so this picture is fed only slow versions of the
            // music (about 0.4 s for the bass and mids, 0.2 s for the bands) and runs on its own, slower clock. The beat reaches it as
            // light that comes up in ~80 ms and goes in ~0.3 s.
            cBass += (bass - cBass) * rate (dt, 3.0f);
            cMid += (mid - cMid) * rate (dt, 3.0f);
            cTreb += (treble - cTreb) * rate (dt, 4.5f);
            cBeat += (beat - cBeat) * rate (dt, beat > cBeat ? 9.0f : 3.0f);   // swells in ~0.1 s, fades in ~0.3 s: clearly there, but no snap
            // How busy the music is: new sounds per second in the range of the voice (quick words, a rap, a drum fill = busy, a long
            // held note = calm). The faster and denser it gets, the faster the picture flows and changes, and the other way round.
            float flux = 0.0f;
            for (int i = 8; i < 21; ++i) flux += std::max (0.0f, view[(size_t) i] - prevView[(size_t) i]);
            for (int i = 0; i < kBands; ++i) prevView[(size_t) i] = view[(size_t) i];
            density += (flux / std::max (dt, 0.001f) - density) * rate (dt, 1.5f);
            const float talk = juce::jlimit (0.0f, 1.0f, density / 10.0f);
            // the character of this song (how busy it is on average) sets the base speed, what is happening now moves it up and down
            ageP += dt;
            if (densP < 0.0f) densP = density;
            densP += (density - densP) * dt / std::max (ageP, 20.0f);
            const float charTalk = juce::jlimit (0.0f, 1.0f, densP / 10.0f);
            pace += ((0.5f + 1.2f * (0.55f * charTalk + 0.45f * talk)) - pace) * rate (dt, pace < 0.5f + 1.2f * (0.55f * charTalk + 0.45f * talk) ? 0.9f : 0.5f);

            // The pulse of the music, tracked: the gaps between the detected beats give the tempo, and a phase clock runs at that tempo
            // and is nudged towards every detected beat. A live stream cannot be read ahead, but a steady beat can be PREDICTED: once
            // the clock has locked on, the picture breathes exactly on the beat (a hair early, to cancel the delay) even where the
            // detector misses one, and it does not lag behind the sound.
            const float pulse = trackPulse (dt);
            // The picture follows the SHAPE of the music over time: a fast and a slow loudness average. When the fast one climbs well
            // above the slow one a new part has begun (a chorus, a drop), when it falls well below, a quiet part has: either way the
            // picture rushes forward for a few seconds and slides on into a different crystal. Its own steady flow does not depend on
            // the music at all (a silent screen still moves); the music only adds to it.
            eFast += (level - eFast) * rate (dt, 1.1f);
            eSlow += (level - eSlow) * rate (dt, 0.10f);
            sinceSection += dt;
            auto& learner = TrackLearner::get();
            const float mapPos = learner.positionSec();
            const bool known = learner.mapReady();
            const float ahead = known ? learner.lookAhead (mapPos, 2.0f) : 0.0f;       // > 0: the song gets louder within two seconds
            const float lift = known ? ahead * 0.25f : eFast - eSlow;                  // a known song announces its parts; a new one is followed
            if (live && sinceSection > 7.0f / pace && (lift > 0.09f || lift < -0.09f)) { sectionTarget += lift > 0 ? 19.0f : 11.0f; sinceSection = 0; }
            sectionShift += (sectionTarget - sectionShift) * rate (dt, 0.28f);
            const float rush = juce::jlimit (0.0f, 1.0f, (sectionTarget - sectionShift) * 0.12f);
            // The words: while a chorus is being sung the picture opens up (brighter, faster, bigger beat); in a verse it draws back.
            {
                float want = learner.loudPartAt (mapPos);                  // no words: the loud, dense parts of the song by its map
                if (false)
                {
                }
                chorusMix += (want - chorusMix) * rate (dt, 1.2f);
            }
            crystalPhase += dt * (0.30f + 0.55f * cBass + 0.7f * rush) * pace * (1.0f + 0.45f * confS * pulse) * (1.0f + 0.5f * chorusMix);   // a swell of speed on every beat
            crystalRot += dt * (0.050f + 0.070f * cMid + 0.07f * rush) * pace;
            for (int i = 0; i < kBands; ++i)
            {
                crystalBands[(size_t) i] += (view[(size_t) i] - crystalBands[(size_t) i]) * rate (dt, 6.0f);
                f.bands[i] = crystalBands[(size_t) i];
            }
            f.phase = crystalPhase; f.rot = crystalRot; f.time = t + sectionShift;
            f.bass = cBass; f.mid = cMid; f.treble = cTreb;
            f.beat = cBeat + (0.85f * pulse - cBeat) * confS;   // locked on: the predicted pulse; otherwise the detected beats
            {
                f.beat = juce::jmin (1.0f, f.beat * (1.0f + 0.5f * chorusMix));
                f.level = juce::jlimit (0.0f, 1.0f, level + 0.35f * chorusMix + 0.25f * std::max (0.0f, ahead));   // light comes up just before the rise
                f.idle = juce::jmax (idle, 0.28f * (1.0f - chorusMix));      // a verse is a little dimmer, a chorus is at full light
            }
        }
        nebula->set (f);
    }
    // ---- the words ------------------------------------------------------------------------------
    void startLyricsFetch (juce::String artist, juce::String title, juce::String album, double durSec)
    {
        lyricsState = 1;
        const int gen = ++fetchGen;
        std::thread ([safe = juce::Component::SafePointer<Visualizer> (this), artist, title, album, durSec, gen]
        {
            std::shared_ptr<LyricsData> data;
            try { data = Lyrics::fetch (artist, title, album, durSec); } catch (...) { data = std::make_shared<LyricsData>(); }
            juce::MessageManager::callAsync ([safe, data, gen]
            {
                if (safe == nullptr || safe->fetchGen != gen) return;       // the song has changed meanwhile
                safe->song = (data != nullptr && data->found) ? data : nullptr;
                safe->lyricsState = 2;
                safe->startAligning();
                safe->startListening();
                if (data != nullptr && data->retry && safe->lyricsTries < 10)          // the server was busy: try again in 15 s, 30 s, 1 min, then every 2 min
                    safe->lyricsRetryAt = juce::Time::getMillisecondCounterHiRes() + 1000.0 * std::min (120.0, 15.0 * std::pow (2.0, (double) safe->lyricsTries++));
            });
        }).detach();
    }
    double positionNow() const
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        double p;
        if (trackTimeline) { p = trackPos; if (trackPlaying) p += (nowMs - trackStamp) / 1000.0; }
        else p = (nowMs - trackSince) / 1000.0;      // the player does not say where it is: take it that the song started when it was noticed
        if (trackTimeline && trackDur > 1.0) p = juce::jmin (p, trackDur);
        return p + 0.25 + lyricOffset + autoShift + VizPrefs::get().lyricLead;    // a line comes up a touch early rather than late; plus the user's nudge, the shift heard and how early the user wants the words
    }
    // where the player was in the song at this moment (millisecond counter), without any shift: for lining the words up with the voice
    double rawPositionAt (double ms) const { return trackPos + (trackPlaying ? (ms - trackStamp) / 1000.0 : 0.0); }

    // The words of a song have come: the aligner learns their line spans, and starts from what it found the last time.
    void startAligning()
    {
        autoShift = 0.0;
        nextAlignAt = 0.0;
        findLineShapes();
        if (song == nullptr) { aligner.reset ({}, 0.0); return; }
        std::vector<std::pair<double, double>> spans;
        std::vector<double> wordStarts;
        for (const auto& ln : song->lines) for (const auto& w : ln.words) if (w.text.trim().isNotEmpty()) wordStarts.push_back (w.t);
        for (size_t i = 0; i < song->lines.size(); ++i)
        {
            const auto& ln = song->lines[i];
            const int words = juce::StringArray::fromTokens (ln.text, " ", "").size();
            if (ln.text.trim().isEmpty() || words == 0) continue;
            if (! ln.words.empty()) { spans.push_back ({ ln.t, ln.words.back().t + ln.words.back().len }); continue; }   // known exactly
            const double nxt = i + 1 < song->lines.size() ? song->lines[i + 1].t : ln.t + 5.0;
            spans.push_back ({ ln.t, ln.t + juce::jmin (nxt - ln.t - 0.1, 0.33 * words + 0.6) });
        }
        // How far the words may be moved, from the two lengths: a video with an intro is longer than the song by about that intro.
        // The database search only takes records within 8 s of what plays, so outside covers a long intro is impossible.
        double maxLead = 0.0, nearSpan = 4.0;
        if (song->cover) { maxLead = LyricAligner::kMaxLead; nearSpan = 6.0; }
        else if (song->durationSec > 1.0 && trackDur > 1.0)
        {
            const double extra = trackDur - song->durationSec;
            maxLead = juce::jlimit (0.0, LyricAligner::kMaxLead, extra + 3.0);
            nearSpan = juce::jlimit (2.5, 6.0, std::abs (extra) + 2.5);
            // word times (NetEase) belong to the studio recording: when what plays is as long as it, it IS that recording and the
            // words can only be a hair off (measured -0.08 s); a longer video may have an intro of about the difference
            if (hasWordTimes())
            {
                maxLead = juce::jlimit (0.0, LyricAligner::kMaxLead, extra + 1.0);
                nearSpan = juce::jlimit (0.5, 6.0, std::abs (extra) + 1.0);
            }
        }
        // Word times (NetEase) are taken as they are when what plays is about as long as the record (within 2 s): measured on the
        // voice they were 0.1 s off at most, while listening for the voice could not see it at all in some songs ("Make Me a Robot":
        // a different answer every 15 s) and moved the words around mid-line. A clip with an intro (longer by 2 s or more) is aligned.
        const bool trustWords = song->heard || hasWordTimes() && ! song->cover && song->durationSec > 1.0 && trackDur > 1.0
                                && std::abs (trackDur - song->durationSec) < 2.0;
        if (trustWords) { aligner.reset ({}, 0.0); autoShift = 0.0; savedShift = 0.0; return; }
        aligner.reset (std::move (spans), trackDur, maxLead, nearSpan, std::move (wordStarts));
        for (const auto& l : juce::StringArray::fromLines (autoShiftFile().loadFileAsString()))
            if (l.startsWith (shiftKey() + "\t") && aligner.preset (l.fromFirstOccurrenceOf ("\t", false, false).getDoubleValue()))
                autoShift = aligner.shift();
        savedShift = autoShift;
    }
    // Every frame of a picture with words: the singing band heard since the last frame goes to the aligner at its place in the song;
    // every 5 s the aligner looks again. Not while the player is paused or does not say where it is.
    void feedAligner()
    {
        StallProbe sp ("Visualizer::feedAligner", 8.0);
        VoiceCapture::get().takeSingingBand (bandTmp);
        if (song == nullptr || ! trackTimeline || ! trackPlaying) return;
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (const auto& b : bandTmp) aligner.add (rawPositionAt (now - b.first * 1000.0), b.second);
        if (now < nextAlignAt) return;
        nextAlignAt = now + 5000.0;
        if (! aligner.update()) return;
        const double was = autoShift;
        autoShift = juce::jlimit (aligner.minShift(), aligner.maxShift(), aligner.shift());   // (a video intro: the song starts up to ~45 s later than the file)
        if (std::abs (autoShift - savedShift) > 0.1 || std::abs (autoShift - was) > 0.5)
        {
            savedShift = autoShift;
            auto lines = juce::StringArray::fromLines (autoShiftFile().loadFileAsString());
            juce::StringArray out;
            for (const auto& l : lines) if (l.isNotEmpty() && ! l.startsWith (shiftKey() + "\t")) out.add (l);
            out.add (shiftKey() + "\t" + juce::String (autoShift, 2));
            autoShiftFile().getParentDirectory().createDirectory();
            autoShiftFile().replaceWithText (out.joinIntoString ("\n") + "\n");
        }
    }
    // ---- the listener (tools\evenly_listen.py, kept in %APPDATA%\Rovno\listen): it finds this very recording on YouTube, hears
    // every word with Whisper and writes the words on its clock (<key>.heard.yrc), a first version after ~30 s of the song and a
    // better one every 30 s after. Started once per song (a ".none" file says it could not), one song at a time, in the background.
    void startListening()
    {
        if (song == nullptr || song->heard || ! trackTimeline || trackDur < 30.0 || song->heardFile == juce::File() || ! song->source.existsAsFile()) return;
        const auto out = song->heardFile;
        if (out.getSiblingFile (out.getFileName() + ".none").existsAsFile()) return;
        const auto script = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("listen").getChildFile ("evenly_listen.py");
        const juce::String py = pythonw();
        if (! script.existsAsFile() || py.isEmpty()) return;
        if (listening().exchange (true)) return;
        auto arg = [] (juce::String v) { v = v.replaceCharacter ('"', '\'').trim(); return v.isEmpty() ? juce::String ("?") : v; };
        juce::String a = trackArtist, ti = trackTitle;
        Lyrics::clean (a, ti);
        const juce::StringArray cmd { py, script.getFullPathName(), "--artist", arg (trackArtist), "--title", arg (ti), "--raw", arg (trackTitle),
                                      "--duration", juce::String (trackDur, 1), "--lyrics", song->source.getFullPathName(), "--out", out.getFullPathName() };
        std::thread ([cmd]
        {
            juce::ChildProcess cp;
            if (cp.start (cmd, 0)) cp.waitForProcessToFinish (20 * 60 * 1000);
            listening() = false;
        }).detach();
    }
    static std::atomic<bool>& listening() { static std::atomic<bool> b { false }; return b; }
    static juce::String pythonw()
    {
        juce::Array<juce::File> found;
        juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", "")).getChildFile ("Programs").getChildFile ("Python")
            .findChildFiles (found, juce::File::findDirectories, false, "Python3*");
        found.sort();
        for (int i = found.size(); --i >= 0;)
            if (found[i].getChildFile ("pythonw.exe").existsAsFile()) return found[i].getChildFile ("pythonw.exe").getFullPathName();
        return {};
    }
    // a new version from the listener for the song that plays: taken at once (the line being sung stays in place)
    void takeHeard()
    {
        if (song == nullptr || song->heardFile == juce::File() || ! song->heardFile.existsAsFile()) return;
        const auto when = song->heardFile.getLastModificationTime();
        if (song->heard && when <= heardStamp) return;
        auto d = Lyrics::parseYrc (song->heardFile.loadFileAsString());
        if (! d->found) return;
        d->heard = true; d->heardFile = song->heardFile; d->source = song->heardFile; d->cover = song->cover;
        heardStamp = when;
        song = d;
        startAligning();
    }

    // the shift heard is kept apart for word-timed lyrics: it is measured on the words, without the 0.4 s the line look adds
    juce::String shiftKey() const { return songKey + (hasWordTimes() ? "|words" : ""); }
    bool hasWordTimes() const
    {
        if (song != nullptr) for (const auto& l : song->lines) if (! l.words.empty()) return true;
        return false;
    }
    static juce::File autoShiftFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("lyrics").getChildFile ("heard_shift.txt"); }
    int lineAt (double pos) const
    {
        if (song == nullptr || song->lines.empty()) return -1;
        int lo = 0, hi = (int) song->lines.size() - 1, ans = -1;
        while (lo <= hi)
        {
            const int m = (lo + hi) / 2;
            if (song->lines[(size_t) m].t <= pos) { ans = m; lo = m + 1; } else hi = m - 1;
        }
        return ans;
    }
    static float textWidth (const juce::Font& fnt, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (fnt, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }
    static float spaceWidth (const juce::Font& fnt) { return juce::jmax (fnt.getHeight() * 0.25f, textWidth (fnt, "i i") - textWidth (fnt, "ii")); }


    // The line that is being sung. With word times (NetEase) every word fills left to right over exactly the time it is sung, like
    // karaoke. Without them (LRCLIB knows only when a LINE starts) the line lights up whole when it starts: spreading the words over
    // the line at a guessed pace ran ahead of or behind the singer. The loudest sharp sounds of the voice give the words a little pop.
    void paintKaraoke (juce::Graphics& g, const LyricLine& ln, double nextT, double pos, juce::Rectangle<float> box, float size, bool chorus)
    {
        juce::ignoreUnused (nextT);
        // the pieces as drawn: each with its own time when known; "gap" = a space follows it
        struct Piece { juce::String s; double t, len; bool gap; double end; };   // len: how long it takes to light; end: when it is sung out
        std::vector<Piece> pcs;
        if (! ln.words.empty())
        {
            for (const auto& w : ln.words)
            {
                const juce::String s = w.text.trim();
                if (s.isEmpty()) { if (! pcs.empty()) pcs.back().gap = true; continue; }
                // the word lights up as it starts (in 0.15 s), not over the whole time it is held: filling a long note to its end looked
                // like the words were dragging behind the singer
                pcs.push_back ({ s, w.t, juce::jlimit (0.06, 0.15, w.len), w.text.endsWithChar (' '), w.t + w.len });
            }
        }
        else
        {
            auto words = juce::StringArray::fromTokens (ln.text, " ", "");
            words.removeEmptyStrings();
            for (const auto& w : words) pcs.push_back ({ w, ln.t, 0.12, true, ln.t });            // all at once, a tenth of a second to come up
        }
        if (pcs.empty()) return;
        // positionNow() runs 0.25 s ahead so a whole line comes up early; a word is filled on its own time (the shift heard puts the
        // word times on the voice), a hair (30 ms) early for the frame and the screen
        const double wpos = ln.words.empty() ? pos : pos - 0.25 + 0.10;     // measured with Whisper on "Dosed": the voice is 0.06 s (0.03-0.23) ahead of NetEase
        auto measure = [&] (float sz, std::vector<float>& wid)
        {
            const juce::Font fnt = font (sz, chorus ? 700 : 600);
            const float sp = spaceWidth (fnt);
            float tot = 0;
            wid.assign (pcs.size(), 0.0f);
            for (size_t i = 0; i < pcs.size(); ++i) { wid[i] = textWidth (fnt, pcs[i].s); tot += wid[i] + (pcs[i].gap && i + 1 < pcs.size() ? sp : 0.0f); }
            return tot;
        };
        std::vector<float> wid;
        float tot = measure (size, wid);
        if (tot > box.getWidth()) { size *= box.getWidth() / tot * 0.98f; tot = measure (size, wid); }
        const juce::Font fnt = font (size, chorus ? 700 : 600);
        const float space = spaceWidth (fnt);
        float x = box.getCentreX() - tot * 0.5f;
        const int baseY = (int) (box.getCentreY() + size * 0.36f);
        const float top = (float) baseY - size * 1.2f, high = size * 1.6f;
        g.setFont (fnt);
        for (size_t i = 0; i < pcs.size(); ++i)
        {
            const auto& p = pcs[i];
            const int xi = (int) x;
            const float fill = juce::jlimit (0.0f, 1.0f, (float) ((wpos - p.t) / p.len));
            g.setColour (juce::Colours::black.withAlpha (0.32f));                      // a shadow, so the words read over the bright picture
            g.drawSingleLineText (p.s, xi + 1, baseY + 3);
            g.setColour (juce::Colours::white.withAlpha (0.34f));                      // not sung yet
            g.drawSingleLineText (p.s, xi, baseY);
            if (! ln.words.empty())
            {
                // Sung like karaoke: the word comes up at once (readable as it starts), then the bright glow runs across it for as long
                // as the note is held ("di-i-ied"), and the word sits a little higher while it is in the singer's mouth.
                if (fill <= 0.0f) { x += wid[i] + (p.gap ? space : 0.0f); continue; }
                const double held = juce::jmax (0.12, p.end - p.t);
                const float sung = juce::jlimit (0.0f, 1.0f, (float) ((wpos - p.t) / held));
                const bool singing = wpos < p.end;
                const int yi = baseY - (singing ? (int) std::lround (size * 0.05f * fill) : 0);
                g.setColour (juce::Colours::white.withAlpha (0.55f * fill));
                g.drawSingleLineText (p.s, xi, yi);
                juce::Graphics::ScopedSaveState keep (g);
                if (sung < 1.0f) g.reduceClipRegion (juce::Rectangle<float> ((float) xi - 3.0f, top - size * 0.1f, wid[i] * sung + 3.0f, high).toNearestInt());
                g.setColour (pa.brighter (0.5f).withAlpha ((singing ? 0.55f : 0.28f) + 0.40f * popEnv));   // the glow, brighter on a sharp sound
                g.drawSingleLineText (p.s, xi - 2, yi);
                g.drawSingleLineText (p.s, xi + 2, yi);
                g.setColour (juce::Colours::white);
                g.drawSingleLineText (p.s, xi, yi);
                x += wid[i] + (p.gap ? space : 0.0f);
                continue;
            }
            if (fill > 0.0f)
            {
                const bool whole = fill >= 1.0f || ln.words.empty();
                juce::Graphics::ScopedSaveState keep (g);
                if (! whole) g.reduceClipRegion (juce::Rectangle<float> ((float) xi - 3.0f, top, wid[i] * fill + 3.0f, high).toNearestInt());
                const float a = ln.words.empty() ? fill : 1.0f;
                const bool singing = ! ln.words.empty() && wpos < p.end;               // the word in the singer's mouth glows the most
                g.setColour (pa.brighter (0.5f).withAlpha (a * ((singing ? 0.45f : 0.28f) + 0.40f * popEnv)));   // the glow, brighter on a sharp sound
                g.drawSingleLineText (p.s, xi - 2, baseY);
                g.drawSingleLineText (p.s, xi + 2, baseY);
                g.setColour (juce::Colours::white.withAlpha (0.96f * a));
                g.drawSingleLineText (p.s, xi, baseY);
            }
            x += wid[i] + (p.gap ? space : 0.0f);
        }
    }

    void paintLyrics (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const float scale = full ? 1.0f : 0.5f;
        if (song == nullptr)
        {
            // no words (instrumental, or not found, or still looking): the name of the song, quietly
            if (trackTitle.isEmpty()) return;
            const float lh = 30.0f * scale + 6.0f;
            const auto box = juce::Rectangle<float> (a.getX() + a.getWidth() * 0.15f, a.getCentreY() + (full ? 70.0f : 30.0f), a.getWidth() * 0.7f, lh);
            g.setFont (font (22.0f * scale + 4.0f, 600));
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawFittedText (trackTitle, box.toNearestInt(), juce::Justification::centred, 1);
            g.setFont (font (14.0f * scale + 3.0f, 400));
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawFittedText (trackArtist + (lyricsState == 1 ? L ("  ·  ищу текст...", "  ·  looking for the words...") : juce::String()), box.translated (0, lh).toNearestInt(), juce::Justification::centred, 1);
            return;
        }
        const double pos = positionNow();
        const int cur = lineAt (pos);
        const float target = (float) juce::jmax (cur, 0);
        lyricScroll += (target - lyricScroll) * rate (dt(), 7.0f);   // lines glide into place (also after a skip in the song)
        const float pitch = full ? 66.0f : 31.0f;
        const float cx = a.getCentreX(), cy = a.getCentreY() - (full ? 14.0f : 8.0f);
        const float maxW = a.getWidth() * (full ? 0.80f : 0.72f);
        const int span = full ? 3 : 1;
        const int base = (int) std::lround (lyricScroll);
        for (int i = base - (full ? 2 : 1); i <= base + span; ++i)
        {
            if (i < 0 || i >= (int) song->lines.size()) continue;
            const auto& ln = song->lines[(size_t) i];
            if (ln.text.isEmpty()) continue;
            const float rel = (float) i - lyricScroll;                       // 0 = the line being sung
            // 1 = this is the line being sung; before the first line (the intro) none is, the first one only waits, dim
            const float focus = cur < 0 ? 0.0f : 1.0f - juce::jlimit (0.0f, 1.0f, std::abs (rel));
            const float fade = rel < 0 ? 0.30f : 0.42f;
            const float alpha = juce::jlimit (0.0f, 1.0f, fade * (1.0f - 0.25f * juce::jmin (3.0f, std::abs (rel))) + (1.0f - fade) * focus);
            const float size = (full ? 40.0f : 19.0f) * (ln.chorus ? 1.22f : 1.0f) * (0.82f + 0.18f * focus);
            const auto box = juce::Rectangle<float> (cx - maxW * 0.5f, cy + rel * pitch - pitch * 0.5f, maxW, pitch).toNearestInt().toFloat();
            if (i == cur && std::abs (rel) < 0.45f)
            {
                const double nextT = i + 1 < (int) song->lines.size() ? song->lines[(size_t) i + 1].t : ln.t + 4.0;
                paintKaraoke (g, ln, nextT, pos, box, size, ln.chorus);
                continue;
            }
            g.setFont (font (size, ln.chorus ? 700 : 500));
            g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
            g.drawFittedText (ln.text, box.translated (1, 3).toNearestInt(), juce::Justification::centred, 2);
            g.setColour (juce::Colours::white.withAlpha (alpha * 0.9f));
            g.drawFittedText (ln.text, box.toNearestInt(), juce::Justification::centred, 2);
        }
    }
    // the tracked pulse of the music (see Crystal): 0..1, one smooth swell per beat, a hair early; confS says how sure it is
    float trackPulse (float dt)
    {
        if (beatEvent)
        {
            if (lastBeatT >= 0.0f)
            {
                float gap = t - lastBeatT;
                while (gap > 0.80f) gap *= 0.5f;
                if (gap >= 0.30f) { gaps.push_back (gap); if (gaps.size() > 12) gaps.pop_front(); }
            }
            lastBeatT = t;
            const float err = phi > 0.5f ? phi - 1.0f : phi;
            phi -= 0.30f * err;
        }
        float lock = 0.0f;
        if (gaps.size() >= 4)
        {
            std::vector<float> g (gaps.begin(), gaps.end());
            std::sort (g.begin(), g.end());
            period = g[g.size() / 2];
            int agree = 0;   // (not "near": windows.h defines that word)
            for (float x : g) if (std::abs (x - period) < 0.12f * period) ++agree;
            lock = (float) agree / (float) g.size();
        }
        if (t - lastBeatT > 3.0f) lock = 0.0f;                      // the beat stopped
        confS += ((lock > 0.6f ? 1.0f : 0.0f) - confS) * rate (dt, 0.8f);
        phi += dt / std::max (0.30f, period);
        phi -= std::floor (phi);
        return std::pow (0.5f + 0.5f * std::cos (6.2831853f * (phi + 0.04f)), 3.0f);
    }
    // "Song": the lively Vortex with the words on top. The chorus opens it up (found from the words, or from the map of the song if there are
    // none), and every sharp sound of the voice kicks it a little.
    void stepSong (float dt, NebulaFrame& f)
    {
        const VoiceFeatures vfs = VoiceCapture::get().features();
        trackVoiceOnsets (dt, vfs);
        feedAligner();
        {
            static const juce::String dpath = juce::SystemStats::getEnvironmentVariable ("ROVNO_VOICELOG", "");
            if (dpath.isNotEmpty() && song != nullptr)
            {
                const bool started = vfs.phraseAge > 0.0f && prevPhraseAge <= 0.0f && vfs.voiced > 0.5f;
                if (started)
                {
                    const double pnow = positionNow() - 0.25;
                    juce::String around;   // (not "near": windows.h defines that word)
                    for (const auto& l : song->lines) if (std::abs (l.t - pnow) < 6.0 && l.text.isNotEmpty()) around << " [" << juce::String (l.t, 1) << "]";
                    juce::File (dpath + ".phr").appendText ("voice phrase starts at " + juce::String (pnow, 1) + " s | lyric lines near it:" + around + "\n", true, false, nullptr);
                }
            }
            prevPhraseAge = vfs.phraseAge;
        }
        auto& learner = TrackLearner::get();
        float want = learner.loudPartAt (learner.positionSec());
        if (song != nullptr)
        {
            const int li = lineAt (positionNow());
            want = (li >= 0 && song->lines[(size_t) li].chorus) ? 1.0f : 0.0f;
        }
        chorusMix += (want - chorusMix) * rate (dt, 1.5f);
        f.beat = juce::jmin (1.0f, f.beat * (1.0f + 0.7f * chorusMix) + 0.15f * onEnv);
        f.bass = juce::jmin (1.0f, f.bass + 0.20f * chorusMix);
        f.level = juce::jlimit (0.0f, 1.0f, level + 0.30f * chorusMix);
        f.treble = juce::jmin (1.0f, f.treble + 0.2f * onEnv * onHiEnv);
        neonPhase += dt * (0.5f * chorusMix) * motion;                 // a chorus flies faster
        f.phase = neonPhase;
    }

    // "Clip", after the video for "Do I Wanna Know?" (ClipLine.h has what the video does and how that was measured). Here the sound
    // and the words of the song are turned into what ClipLine needs, every frame.
    void stepClip (float dt, NebulaFrame& f)
    {
        static_assert (ClipLine::N == NebulaFrame::kClipN, "the picture and the renderer must agree on the number of points");
        static_assert (ClipLine::kSlots == NebulaFrame::kClipSlots, "and on the number of lines");
        VoiceCapture& cap = VoiceCapture::get();
        const VoiceFeatures vf = cap.features();                // (also keeps the listening going)
        feedAligner();
        ClipInput in;
        in.dt = dt;
        in.aspect = juce::jmax (0.5f, width() / juce::jmax (1.0f, height()));
        // new sounds: mostly from the levels the effect measures (the voice analyser finds a syllable only now and then in a full mix)
        clipOnsets.step (bandsDb.data(), kBands, dt);
        if (live && clipOnsets.onset) { in.onset = true; in.onsetStrength = clipOnsets.strength; in.onsetHigh = clipOnsets.high; }
        if (vf.onsetSeq != clipOnSeq)
        {
            if (clipOnSeq != 0 && ! in.onset) { in.onset = true; in.onsetStrength = vf.onsetStrength; in.onsetHigh = vf.onsetHigh; }
            clipOnSeq = vf.onsetSeq;
        }
        in.voiced = vf.voiced; in.intensity = vf.intensity; in.hold = vf.hold; in.reg = vf.reg;
        in.level = juce::jlimit (0.0f, 1.0f, level * 1.3f);
        in.beatPulse = trackPulse (dt); in.beatPeriod = confS > 0.3f ? period : 0.0f; in.beatConf = confS;
        in.bright = centroid;
        clipSinceVoice = vf.voiced > 0.5f ? 0.0f : clipSinceVoice + dt;
        if (song != nullptr && ! song->lines.empty())
        {
            const double pos = positionNow();
            const int li = lineAt (pos);
            in.haveWords = true;
            if (li >= 0)
            {
                const auto& ln = song->lines[(size_t) li];
                const double nextT = li + 1 < (int) song->lines.size() ? song->lines[(size_t) li + 1].t : ln.t + 5.0;
                const int words = juce::StringArray::fromTokens (ln.text, " ", "").size();
                const bool hasText = ln.text.trim().isNotEmpty();
                in.singing = hasText && pos < ln.t + juce::jmin (nextT - ln.t - 0.1, 0.33 * words + 0.6);
                in.lineStarted = hasText && li != clipLineSeen;
                in.chorus = ln.chorus;
                in.backing = ln.text.trim().startsWithChar ('(');
                if ((size_t) li < lineAct.size()) { in.act = lineAct[(size_t) li]; in.stave = lineStave[(size_t) li] != 0; in.staveUp = lineStave[(size_t) li] == 2; in.hue = lineHue[(size_t) li]; in.world = lineWorld[(size_t) li] != 0; in.burst = lineBurst[(size_t) li] != 0; }
                if (in.backing)
                {
                    // the bracketed words come first: the blue backing voice sings them, then the lead goes on (as in the video)
                    const juce::String tx = ln.text.trim();
                    const int close = tx.indexOfChar (')');
                    const float inside = (float) juce::StringArray::fromTokens (tx.substring (0, juce::jmax (0, close)), " ", "").size();
                    const float frac = close > 0 ? inside / (float) juce::jmax (1, words) : 1.0f;
                    in.backingNow = in.singing && pos < ln.t + frac * juce::jmin (nextT - ln.t - 0.1, 0.33 * words + 0.6) + 0.15;
                }
                in.lineLeft = (float) (nextT - pos);
                if ((size_t) li < lineShapes.size()) { in.lineShape = lineShapes[(size_t) li].first; in.lineShape2 = lineShapes[(size_t) li].second; }
                // a video can start the song later than the words file (an intro): until the voice has placed the words, a drawing
                // only comes while a voice is actually heard
                if (! aligner.ready() && std::abs (autoShift) < 0.01 && clipSinceVoice > 1.5f) { in.lineShape = -1; in.lineShape2 = -1; }
                if (ln.chorus) chorusPassed = true;
            }
            clipLineSeen = li;
            for (size_t k = (size_t) juce::jmax (0, li + 1); k < song->lines.size(); ++k)
                if (song->lines[k].text.trim().isNotEmpty()) { in.nextLineIn = (float) (song->lines[k].t - pos); break; }
            in.storyOpen = true;   // (it used to wait for the first chorus; the user wants the pictures of the words from the start)
            in.motif = songMotif;
            // the end, as the video ends: its last image (the song's own; the chequered flags for the band of that video), then
            // the initials of the artist drawn by the line ("AM", "RHCP", "MGMT")
            if (trackDur > 60.0)
            {
                const bool am = trackArtist.containsIgnoreCase ("arctic monkeys");
                if (pos > trackDur - 4.5 && songInitials.isNotEmpty()) in.forceLetters = songInitials.toStdString();
                else if (pos > trackDur - 10.0) in.forceShape = am ? ClipLine::shapeNamed ("flag") : songMotif;
            }
        }
        // for checking the picture on a silent machine (never play sound to check it): ROVNO_CLIPDEMO=1 makes up syllables,
        // a swoop every 4 s, a chorus from 12 s and a drawing from 20 s
        static const bool demo = juce::SystemStats::getEnvironmentVariable ("ROVNO_CLIPDEMO", "").isNotEmpty();
        if (demo)
        {
            clipDemoT += dt;
            const float tt = clipDemoT;
            const float ph = std::fmod (tt, 0.45f);
            in.onset = ph < dt; in.onsetStrength = 0.6f; in.onsetHigh = 0.3f;
            in.voiced = ph < 0.32f ? 1.0f : 0.0f; in.intensity = 0.5f + 0.4f * std::sin (tt * 0.7f); in.reg = 0.5f; in.level = 0.6f;
            in.haveWords = true; in.singing = true;
            in.nextLineIn = 4.0f - std::fmod (tt, 4.0f);
            in.lineStarted = std::fmod (tt, 4.0f) < dt;
            in.chorus = tt > 12.0f && tt < 20.0f;
            in.storyOpen = true; in.lineLeft = 3.5f;
            in.lineShape = ClipLine::shapeNamed (tt < 28.0f ? "heart" : "phone");
            if (tt < 20.0f) in.lineShape = -1;
        }
        ClipLine::Output& o = clipOut;
        clipLine.step (in, o);
        {
            // (only with ROVNO_VOICELOG set) twice a second: how many sounds started and how far the line moved, to check it on real music
            static const juce::String dpath = juce::SystemStats::getEnvironmentVariable ("ROVNO_VOICELOG", "");
            if (dpath.isNotEmpty())
            {
                clipLogOn += in.onset ? 1 : 0;
                for (int i = 0; i < ClipLine::N; i += 4) clipLogMax = std::max (clipLogMax, std::abs (o.pts[0][i * 2 + 1]));
                if ((clipLogT += dt) > 0.5f)
                {
                    juce::File (dpath + ".clip").appendText (juce::String::formatted ("live %d level %.2f onsets %d voiced %.2f words %d line %.3f lines %d\n",
                        live ? 1 : 0, level, clipLogOn, vf.voiced, in.haveWords ? 1 : 0, clipLogMax, o.lines), true, false, nullptr);
                    clipLogT = 0; clipLogOn = 0; clipLogMax = 0;
                }
            }
        }
        f.clipLines = o.lines;
        for (int L = 0; L < o.lines; ++L)
        {
            std::memcpy (f.clipPts[L], o.pts[L], sizeof (o.pts[L]));
            std::memcpy (f.clipCol[L], o.col[L], sizeof (o.col[L]));
            f.clipAlpha[L] = o.alpha[L];
        }
        for (int c = 0; c < 3; ++c) f.clipBg[c] = o.bg[c];
        f.clipInk = o.ink;
        f.phase = neonPhase;
    }
    // the drawing each line of the words calls for, found once when the words come
    void findLineShapes()
    {
        lineShapes.clear();
        chorusPassed = false; clipLineSeen = -1;
        if (song == nullptr) return;
        for (const auto& ln : song->lines) lineShapes.push_back (ClipLine::shapesFor (ln.text.toLowerCase().toStdString()));
        {
            std::vector<std::string> texts; std::vector<char> ch; std::vector<double> ts;
            for (const auto& ln : song->lines) { texts.push_back (ln.text.toLowerCase().toStdString()); ch.push_back (ln.chorus ? 1 : 0); ts.push_back (ln.t); }
            const auto plan = ClipLine::planSong (texts, ch, ts);
            lineAct = plan.act; lineStave = plan.stave; lineHue = plan.hue; lineWorld = plan.world; lineBurst = plan.burst;
        }        // the image of the song: the drawing its words call for most often (at least twice), weighted to the chorus
        std::map<int, float> votes;
        for (size_t q = 0; q < lineShapes.size(); ++q)
            if (lineShapes[q].first >= 0) votes[lineShapes[q].first] += song->lines[q].chorus ? 2.0f : 1.0f;
        songMotif = -1; float top = 1.5f;
        for (const auto& [shp, v] : votes) if (v > top) { top = v; songMotif = shp; }
        // the initials of the artist, for the end: "Red Hot Chili Peppers" -> RHCP, one short name stays whole (MGMT, Muse -> M)
        songInitials = {};
        {
            juce::String a = trackArtist, tt = trackTitle;
            Lyrics::clean (a, tt);
            auto words = juce::StringArray::fromTokens (a.toUpperCase(), " &-,.", "");
            words.removeEmptyStrings();
            words.removeString ("THE");
            if (words.size() == 1 && words[0].length() <= 4 && words[0].containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZ")) songInitials = words[0];
            else for (const auto& w : words) if (songInitials.length() < 4 && w[0] >= 'A' && w[0] <= 'Z') songInitials += juce::String::charToString (w[0]);
        }
    }

    void stepWave (float dt, NebulaFrame& f)
    {
        VoiceCapture& cap = VoiceCapture::get();
        const VoiceFeatures vf = cap.features();                // (also keeps the listening going)
        float raw[512];
        if (! cap.waveform (raw)) for (float& x : raw) x = 0.0f;
        for (int i = 0; i < 512; ++i) { waveSm[i] += (raw[i] - waveSm[i]) * rate (dt, 30.0f); f.wave[i] = waveSm[i]; }
        // how full the music is compared with a moment ago: the fast loudness climbing above the slow one means more has come in
        waveFast += (level - waveFast) * rate (dt, 1.4f);
        waveSlow += (level - waveSlow) * rate (dt, 0.18f);
        float want = juce::jlimit (0.0f, 1.0f, (waveFast - waveSlow) * 9.0f);
        want = juce::jmax (want, TrackLearner::get().loudPartAt (TrackLearner::get().positionSec()));
        if (song != nullptr)
        {
            const int li = lineAt (positionNow());
            if (li >= 0 && song->lines[(size_t) li].chorus) want = juce::jmax (want, 0.9f);
        }
        waveChorus += (want - waveChorus) * rate (dt, want > waveChorus ? 1.5f : 0.6f);
        waveVoice += (vf.voiced - waveVoice) * rate (dt, 6.0f);
        f.wVoice = juce::jlimit (0.0f, 1.0f, waveVoice * 1.2f);
        f.wChorus = waveChorus;
        f.wAmp = 0.05f + 0.20f * juce::jlimit (0.0f, 1.0f, level * 1.5f);
        f.phase = neonPhase;
    }

    // "Voice": what the singer is doing. The slow things (pitch, held notes, mood) are eased; the SHARP things (a new sound starting)
    // are not: they come through at once and die away in about a tenth of a second, so the picture answers every syllable.
    void stepVoice (float dt, NebulaFrame& f)
    {
        const VoiceFeatures vf = VoiceCapture::get().features();
        static const bool logIt = juce::SystemStats::getEnvironmentVariable ("ROVNO_VOICELOG", "").isNotEmpty();
        if (logIt && (++logTick % 30) == 0)   // a line every half second, for checking the voice analysis against real music
            juce::File (juce::SystemStats::getEnvironmentVariable ("ROVNO_VOICELOG", "")).appendText (
                juce::String::formatted ("voiced %.2f midi %.1f reg %.2f slope %.1f hold %.2f vib %.2f build %.2f int %.2f bright %.2f tens %.2f age %.1f end %.2f onsets %u live %d\n",
                    vf.voiced, vf.midi, vf.reg, vf.slope, vf.hold, vf.vibrato, vf.build, vf.intensity, vf.bright, vf.tension, vf.phraseAge, vf.endPulse,
                    (unsigned) vf.onsetSeq, VoiceCapture::get().listening() ? 1 : 0));
        auto ease = [&] (float& cur, float target, float perSecond) { cur += (target - cur) * rate (dt, perSecond); };
        ease (sv[0], vf.voiced, 18.0f);   ease (sv[1], vf.reg, 7.0f);       ease (sv[2], vf.slope, 8.0f);
        ease (sv[3], vf.hold, 7.0f);      ease (sv[4], vf.vibrato, 6.0f);   ease (sv[5], vf.build, 5.0f);
        ease (sv[6], vf.intensity, 10.0f); ease (sv[7], vf.bright, 8.0f);   ease (sv[8], vf.tension, 2.0f);
        ease (sv[9], vf.endPulse, 25.0f);
        trackVoiceOnsets (dt, vf);
        f.vVoiced = sv[0]; f.vReg = sv[1]; f.vSlope = sv[2]; f.vHold = sv[3]; f.vVib = sv[4]; f.vBuild = sv[5];
        f.vInt = sv[6]; f.vBright = sv[7]; f.vTens = sv[8]; f.vEnd = sv[9]; f.vX = 0.0f; f.vY = 0.0f;
        f.vOnset = onEnv; f.vOnHi = onHiEnv; f.vSeed = onSeed;
        f.phase = neonPhase;
        f.beat = beat * 0.6f;                         // the music is only a quiet pulse under the voice here
    }
    // every new sharp sound sets the envelope to its strength at once; it then dies away fast
    void trackVoiceOnsets (float dt, const VoiceFeatures& vf)
    {
        if (vf.onsetSeq != onSeqSeen)
        {
            if (onSeqSeen != 0 || vf.onsetSeq < 4) { onTarget = juce::jmax (onTarget, 0.35f + 0.45f * vf.onsetStrength); onHiEnv = vf.onsetHigh; onSeed = std::fmod ((float) vf.onsetSeq * 0.618034f, 1.0f); popEnv = juce::jmax (popEnv, 0.7f); }
            onSeqSeen = vf.onsetSeq;
        }
        // a sound swells in over ~40 ms and fades over ~0.3 s: it answers every syllable, but nothing ever snaps
        onTarget *= std::exp (-dt * 5.0f);
        onEnv += (onTarget - onEnv) * rate (dt, 22.0f);
        popEnv *= std::exp (-dt * 5.0f);
    }    // the nudge of the words is remembered per song
    static juce::File offsetFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("lyrics").getChildFile ("offsets.txt"); }
    void saveOffset()
    {
        auto lines = juce::StringArray::fromLines (offsetFile().loadFileAsString());
        juce::StringArray out;
        for (const auto& l : lines) if (l.isNotEmpty() && ! l.startsWith (songKey + "\t")) out.add (l);
        if (lyricOffset != 0.0) out.add (songKey + "\t" + juce::String (lyricOffset, 2));
        offsetFile().getParentDirectory().createDirectory();
        offsetFile().replaceWithText (out.joinIntoString ("\n") + "\n");
    }
    void loadOffset()
    {
        lyricOffset = 0.0;
        for (const auto& l : juce::StringArray::fromLines (offsetFile().loadFileAsString()))
            if (l.startsWith (songKey + "\t")) lyricOffset = l.fromFirstOccurrenceOf ("\t", false, false).getDoubleValue();
    }

    // diagnostics (only with ROVNO_VOICELOG set): how long a frame takes to paint, and what the Song picture thinks it is showing
    void noteFrame (double paintStart)
    {
        static const juce::String path = juce::SystemStats::getEnvironmentVariable ("ROVNO_VOICELOG", "");
        if (path.isEmpty()) return;
        const double now = juce::Time::getMillisecondCounterHiRes();
        paintMs = juce::jmax (paintMs, (float) (now - paintStart));
        if (lastTickMsDiag > 0) gapMs = juce::jmax (gapMs, (float) (now - lastTickMsDiag));
        lastTickMsDiag = now;
        if (now - lastDiagLog > 1000.0)
        {
            lastDiagLog = now;
            juce::String cur;
            if (song != nullptr) { const int li = lineAt (positionNow()); if (li >= 0) cur = song->lines[(size_t) li].text; }
            juce::File (path + ".song").appendText (juce::String ("style ") + juce::String ((int) style) + " | " + trackArtist + " - " + trackTitle + " | words "
                + (lyricsState == 2 ? (song != nullptr ? "found" : "none") : (lyricsState == 1 ? "looking" : "-")) + " (" + juce::String (song != nullptr ? (int) song->lines.size() : 0)
                + " lines) | pos " + juce::String (positionNow(), 1) + (trackTimeline ? " (player)" : " (guessed)") + " dur " + juce::String (trackDur, 0)
                + " | heard shift " + juce::String (autoShift, 2) + (aligner.ready() ? " (q " + juce::String (aligner.quality(), 2) + ")" : " (not yet)")
                + " | line: " + cur + " | paint max " + juce::String (paintMs, 0) + " ms, gap max " + juce::String (gapMs, 0) + " ms\n", true, false, nullptr);
            paintMs = 0; gapMs = 0;
        }
    }
    float prevPhraseAge = 0.0f;
    float waveSm[512] {};
    float waveFast = 0.0f, waveSlow = 0.0f, waveChorus = 0.0f, waveVoice = 0.0f;                         // the "Wave" picture
    double lyricOffset = 0.0;                                                          // the user's nudge of the words for this song, seconds
    LyricAligner aligner;                                                              // where the lines really are, by the voice that is heard
    double autoShift = 0.0, savedShift = 0.0, nextAlignAt = 0.0;    std::vector<std::pair<double, float>> bandTmp;
    float offsetFlash = 0.0f;
    float paintMs = 0, gapMs = 0; double lastTickMsDiag = 0, lastDiagLog = 0;
    float width() const { return (float) getWidth(); }
    float height() const { return (float) getHeight(); }
    void poke()
    {
        hoverTarget = true;
        lastMove = juce::Time::getMillisecondCounter();
        if (full) setMouseCursor (juce::MouseCursor::NormalCursor);
    }
    void step (int dir)
    {
        const auto s = (VizStyle) (((int) style + dir + kVizStyles) % kVizStyles);
        requestStyle (s);
        if (onChoice) onChoice (s, autoCycle);
    }
    void requestStyle (VizStyle s) { if (s != style || pending >= 0) pending = (int) s; }
    void applyStyle (VizStyle s)
    {
        style = s;
        if ((s == VizStyle::Song || s == VizStyle::Wave) && song == nullptr) lyricsState = 0;   // ask for the words again (they were not wanted before)
        nameFlash = 1.6f;
        sinceSwitch = 0;
        fbA = juce::Image(); fbB = juce::Image();   // the feedback picture starts over
        shocks.clear();
        stars.clear();
        rows.clear();
    }

    // ---- the music, as numbers ------------------------------------------------------------------
    void measure (float dt)
    {
        constexpr float lo = -64.0f, hi = -10.0f;
        std::array<float, (size_t) kBands> raw {};
        float mx = 0;
        for (size_t i = 0; i < (size_t) kBands; ++i)
        {
            raw[i] = live ? juce::jlimit (0.0f, 1.0f, (bandsDb[i] - lo) / (hi - lo)) : 0.0f;
            mx = std::max (mx, raw[i]);
        }
        // follow the loudness of the material: quiet old recordings fill the picture just like loud ones
        peakEnv = std::max (mx, peakEnv - dt * 0.04f);
        peakEnv = std::max (peakEnv, 0.5f);
        const float norm = 1.0f / peakEnv;
        for (size_t i = 0; i < (size_t) kBands; ++i)
        {
            const float v = std::pow (juce::jlimit (0.0f, 1.0f, raw[i] * norm), 1.35f);
            const float fall = i < 8 ? 4.5f : i < 18 ? 6.5f : 9.0f;     // bass hangs on, treble is quick
            bar[i] += (v - bar[i]) * rate (dt, v > bar[i] ? 120.0f : fall);
        }
        const bool silent = ! live || mx < 0.05f;
        idle += ((silent ? 1.0f : 0.0f) - idle) * rate (dt, 1.6f);
        for (size_t i = 0; i < (size_t) kBands; ++i)
            view[i] = std::max (bar[i], idle * (0.10f + 0.05f * std::sin (t * 0.7f + (float) i * 0.45f)));

        auto avg = [&] (const std::array<float, (size_t) kBands>& a, int from, int to)
        {
            float s = 0;
            for (int i = from; i < to; ++i) s += a[(size_t) i];
            return s / (float) (to - from);
        };
        bass = avg (view, 0, 8);
        mid = avg (view, 8, 18);
        treble = avg (view, 18, kBands);
        level = juce::jlimit (0.0f, 1.0f, bass * 0.5f + mid * 0.35f + treble * 0.15f);
        float num = 0, den = 1.0e-4f;
        for (int i = 0; i < kBands; ++i) { num += (float) i * bar[(size_t) i]; den += bar[(size_t) i]; }
        if (den > 0.05f) centroid += (num / den / (float) (kBands - 1) - centroid) * rate (dt, 2.0f);

        // beat: the bass jumps above its own recent average by more than its usual wobble
        const float bNow = avg (raw, 0, 8) * norm;
        const float d = bNow - bassAvg;
        bassAvg += d * rate (dt, 2.0f);
        bassVar += (d * d - bassVar) * rate (dt, 2.0f);
        sinceBeat += dt;
        beatEvent = false;
        const float gainK = kind == Kind::Speech ? 0.5f : 1.0f;
        if (live && bNow > 0.25f && d > std::max (0.07f, 1.3f * std::sqrt (bassVar)) && sinceBeat > 0.22f)
        {
            beat = gainK;
            sinceBeat = 0;
            beatEvent = true;
        }
        beat *= std::exp (-dt * 4.2f);

        // the character of what is playing
        motion = kind == Kind::Speech ? 0.55f : kind == Kind::Movie ? 0.8f : kind == Kind::Game ? 1.15f : 1.0f;
        motion *= 0.35f + 0.65f * (1.0f - idle);
        // the mood of the song: its loudness over the last ~12 s takes the colour from cold and deep blue (quiet, slow) to hot red (loud, hard)
        moodE += (level - moodE) * rate (dt, 0.12f);
        moodHue = 0.64f - 0.62f * juce::jlimit (0.0f, 1.0f, (moodE - 0.12f) / 0.40f);
        const float wantHue = style == VizStyle::Song ? moodHue : style == VizStyle::Voice ? 0.64f - 0.62f * juce::jlimit (0.0f, 1.0f, (sv[8] - 0.25f) / 0.45f) : kindColour (kind).getHue();
        float dh = wantHue - baseHue;
        if (dh > 0.5f) dh -= 1.0f;
        if (dh < -0.5f) dh += 1.0f;
        baseHue += dh * rate (dt, 2.5f);
        baseHue -= std::floor (baseHue);
        const float shift = (centroid - 0.45f) * 0.16f + 0.035f * std::sin (t * 0.06f);
        const float sat = 0.55f + 0.30f * level;
        const float bright = 0.62f + 0.38f * juce::jmax (level, 1.0f - idle * 0.6f) * (1.0f - idle * 0.45f);
        pa = juce::Colour::fromHSV (wrap (baseHue + shift), sat, bright, 1.0f);
        pb = juce::Colour::fromHSV (wrap (baseHue + shift + 0.15f), sat + 0.05f, bright * 0.97f, 1.0f);
        pc = juce::Colour::fromHSV (wrap (baseHue + shift - 0.13f), sat - 0.1f, bright, 1.0f);
        bg = col::bg;
        if (follow && ! idleLook())
        {
            ScreenPalette::Palette sp;
            if (ScreenPalette::get().read (sp) && sp.confidence > 0.06f)
            {
                // the video's own colours, eased in; a screen with little colour fades back to the mode colours
                const float k = rate (dt, 2.6f) * juce::jlimit (0.15f, 1.0f, sp.confidence * 2.0f);
                if (! palSeeded) { palA = sp.colours[0]; palB = sp.colours[1]; palC = sp.colours[2]; palSeeded = true; }
                palA = palA.interpolatedWith (sp.colours[0], k);
                palB = palB.interpolatedWith (sp.colours[1], k);
                palC = palC.interpolatedWith (sp.colours[2], k);
                const float mix = juce::jlimit (0.0f, 1.0f, sp.confidence * 2.5f);
                pa = pa.interpolatedWith (palA, mix);
                pb = pb.interpolatedWith (palB, mix);
                pc = pc.interpolatedWith (palC, mix);
            }
        }
    }
    static float wrap (float h) { return h - std::floor (h); }
    bool idleLook() const { return false; }
    float sampleBar (float pos) const   // pos 0..1 across the bands, interpolated
    {
        const float f = juce::jlimit (0.0f, 1.0f, pos) * (float) (kBands - 1);
        const int i = (int) f;
        const float a = f - (float) i;
        return view[(size_t) i] * (1 - a) + view[(size_t) std::min (kBands - 1, i + 1)] * a;
    }
    float scaleF() const { return juce::jlimit (0.7f, 2.2f, std::min (width() / 592.0f, height() / 150.0f * 0.9f)); }

    // ---- Flow: a picture that keeps zooming and turning into itself (MilkDrop's idea) -----------------
    struct Shock { float r, a; };
    void stepFlow (float dt)
    {
        const int fw = juce::jlimit (160, 640, getWidth());
        const int fh = std::max (60, fw * getHeight() / std::max (1, getWidth()));
        if (! fbA.isValid() || fbA.getWidth() != fw || fbA.getHeight() != fh)
        {
            fbA = juce::Image (juce::Image::ARGB, fw, fh, true);
            fbB = juce::Image (juce::Image::ARGB, fw, fh, true);
            juce::Graphics(fbA).fillAll (bg.interpolatedWith (juce::Colours::black, 0.35f));
            shocks.clear();
        }
        juce::Graphics g (fbB);
        const float cx = (float) fw * 0.5f, cy = (float) fh * 0.5f;
        const float sc = (float) fw / 592.0f;
        const float zoom = 1.0f + dt * (0.16f + 0.5f * bass + 1.0f * beat) * motion;
        const float rot = dt * (0.12f * std::sin (t * 0.37f) + 0.35f * (mid - 0.3f)) * motion;
        const float dx = 6.0f * sc * std::sin (t * 0.23f) * dt, dy = 4.0f * sc * std::cos (t * 0.31f) * dt;
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImageTransformed (fbA, juce::AffineTransform::rotation (rot, cx, cy).scaled (zoom, zoom, cx, cy).translated (dx, dy), false);
        g.setColour (bg.interpolatedWith (juce::Colours::black, 0.35f).withAlpha (0.07f + 0.05f * (1.0f - level)));
        g.fillRect (0, 0, fw, fh);

        const float R = (float) std::min (fw, fh) * (0.20f + 0.05f * bass + 0.08f * beat);
        const int n = 120;
        const float spin = t * 0.18f * motion;
        auto ring = [&] (float radius, float amp, float phase, juce::Colour c, float alpha, float width)
        {
            juce::Path p;
            for (int k = 0; k <= n; ++k)
            {
                const float u = (float) k / (float) n;
                const float u3 = std::fmod (u * 3.0f + phase, 1.0f);
                const float v = sampleBar (std::abs (2.0f * u3 - 1.0f));
                const float ang = u * juce::MathConstants<float>::twoPi + spin;
                const float r = radius * (1.0f + amp * v);
                const float x = cx + r * std::cos (ang), y = cy + r * std::sin (ang);
                if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            p.closeSubPath();
            g.setColour (c.withAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (width * sc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        g.setGradientFill (juce::ColourGradient (pa.withAlpha (0.10f + 0.28f * beat + 0.10f * bass), cx, cy, pa.withAlpha (0.0f), cx + R * 0.9f, cy, true));
        g.fillEllipse (cx - R * 0.9f, cy - R * 0.9f, R * 1.8f, R * 1.8f);
        ring (R, 0.95f, 0.0f, pa, 0.95f, 2.0f);
        ring (R * 0.55f, 0.8f, 0.5f, pb, 0.75f, 1.6f);
        if (treble > 0.12f) ring (R * 1.5f, 0.35f, 0.25f, pc, 0.25f + 0.5f * treble, 1.2f);
        if (beatEvent) shocks.push_back ({ R * 0.9f, 0.9f });
        for (auto& s : shocks) { s.r += dt * (float) fw * 0.30f; s.a -= dt * 1.4f; }
        shocks.erase (std::remove_if (shocks.begin(), shocks.end(), [] (const Shock& s) { return s.a <= 0.0f; }), shocks.end());
        for (const auto& s : shocks)
        {
            g.setColour (pc.withAlpha (s.a * 0.8f));
            g.drawEllipse (cx - s.r, cy - s.r, s.r * 2, s.r * 2, 2.0f * sc);
        }
        std::swap (fbA, fbB);
    }
    void paintFlow (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (! fbA.isValid()) return;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (fbA, area);
    }

    // ---- Pulse: a ring that breathes with the bass, light bars all around --------------------------
    void paintPulse (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const auto c = a.getCentre();
        const float R = std::min (a.getWidth(), a.getHeight()) * 0.5f;
        const float r0 = R * (0.42f + 0.05f * bass + 0.07f * beat);
        const float sc = scaleF();
        // the glow in the middle
        g.setGradientFill (juce::ColourGradient (pa.withAlpha (0.10f + 0.30f * beat + 0.12f * bass), c.x, c.y, pa.withAlpha (0.0f), c.x + r0 * 1.9f, c.y, true));
        g.fillEllipse (c.x - r0 * 1.9f, c.y - r0 * 1.9f, r0 * 3.8f, r0 * 3.8f);
        g.setColour (pb.withAlpha (0.18f + 0.35f * beat));
        g.drawEllipse (c.x - r0, c.y - r0, r0 * 2, r0 * 2, 1.6f * sc);
        const int m = 72;
        const float spin = t * 0.10f * motion - juce::MathConstants<float>::halfPi;
        for (int k = 0; k < m; ++k)
        {
            const float u = (float) k / (float) m;
            const float mirrored = std::abs (2.0f * u - 1.0f);                  // bass at the bottom, treble on top
            const float v = sampleBar (1.0f - mirrored);
            const float ang = u * juce::MathConstants<float>::twoPi + spin;
            const float len = (R - r0) * 0.98f * v + 2.5f * sc;
            const juce::Point<float> d (std::cos (ang), std::sin (ang));
            const juce::Colour cc = mirrored < 0.5f ? pa.interpolatedWith (pb, mirrored * 2.0f) : pb.interpolatedWith (pc, mirrored * 2.0f - 1.0f);
            juce::Path spoke;
            spoke.startNewSubPath (c + d * r0);
            spoke.lineTo (c + d * (r0 + len));
            g.setColour (cc.withAlpha (0.35f + 0.6f * v));
            g.strokePath (spoke, juce::PathStrokeType (std::max (2.0f, 2.8f * sc), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path in;                                                     // a short reflection inside the ring
            in.startNewSubPath (c + d * (r0 - 3.0f * sc));
            in.lineTo (c + d * (r0 - 3.0f * sc - len * 0.28f));
            g.setColour (cc.withAlpha (0.16f + 0.2f * v));
            g.strokePath (in, juce::PathStrokeType (std::max (1.5f, 2.0f * sc), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (v > 0.55f)
            {
                const auto tip = c + d * (r0 + len + 6.0f * sc);
                g.setColour (cc.withAlpha (0.8f));
                g.fillEllipse (tip.x - 1.6f * sc, tip.y - 1.6f * sc, 3.2f * sc, 3.2f * sc);
            }
        }
    }

    // ---- Aurora: layers of silk, each one carried by its own part of the spectrum -----------------------
    void paintAurora (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const int layers = 5, pts = 72;
        const float w = a.getWidth(), h = a.getHeight();
        for (int j = layers - 1; j >= 0; --j)
        {
            const float fj = (float) j / (float) (layers - 1);                  // 0 front .. 1 back
            const float baseY = a.getY() + h * (0.78f - 0.10f * (float) j) - h * 0.05f * bass;
            const float amp = h * (0.50f - 0.05f * (float) j);
            const float speed = (0.16f + 0.07f * (float) j) * motion;
            juce::Path top;
            for (int i = 0; i <= pts; ++i)
            {
                const float u = (float) i / (float) pts;
                const float centre = std::abs (2.0f * std::fmod (u * (1.0f + 0.3f * (float) j) + t * speed * 0.15f + 0.13f * (float) j, 1.0f) - 1.0f);
                const float v = sampleBar (centre);
                const float wave = 0.55f + 0.45f * std::sin (u * (5.0f + 2.0f * (float) j) + t * speed * 3.0f + (float) j);
                const float y = baseY - amp * v * wave * (0.55f + 0.5f * (1.0f - fj)) - h * 0.06f * std::sin (u * 3.0f + t * speed + (float) j);
                const float x = a.getX() + u * w;
                if (i == 0) top.startNewSubPath (x, y); else top.lineTo (x, y);
            }
            juce::Path fill (top);
            fill.lineTo (a.getRight(), a.getBottom());
            fill.lineTo (a.getX(), a.getBottom());
            fill.closeSubPath();
            const juce::Colour cc = j % 3 == 0 ? pa : j % 3 == 1 ? pb : pc;
            g.setGradientFill (juce::ColourGradient (cc.withAlpha (0.34f), 0, baseY - amp, cc.withAlpha (0.0f), 0, a.getBottom(), false));
            g.fillPath (fill);
            g.setColour (cc.brighter (0.2f).withAlpha (0.55f + 0.35f * (1.0f - fj)));
            g.strokePath (top, juce::PathStrokeType (std::max (1.2f, 1.7f * scaleF()), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // a few stars that blink with the treble
        juce::Random r (17);
        const int n = full ? 60 : 24;
        for (int i = 0; i < n; ++i)
        {
            const float x = a.getX() + r.nextFloat() * w, y = a.getY() + r.nextFloat() * h * 0.55f;
            const float tw = 0.5f + 0.5f * std::sin (t * (0.7f + r.nextFloat() * 1.6f) + (float) i);
            g.setColour (juce::Colours::white.withAlpha ((0.08f + 0.5f * treble) * tw));
            const float s = (0.8f + r.nextFloat() * 1.2f) * scaleF();
            g.fillEllipse (x, y, s, s);
        }
    }

    // ---- Stars: flying through a field of light, faster with the music -------------------------------
    struct Star { float angle, r, speed, mix; };
    void respawn (Star& s, bool centre)
    {
        s.angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        s.r = centre ? 0.01f + rng.nextFloat() * 0.05f : rng.nextFloat();
        s.speed = 0.6f + rng.nextFloat() * 0.9f;
        s.mix = rng.nextFloat();
    }
    void stepStars (float dt)
    {
        const size_t want = full ? 420 : 150;
        if (stars.size() != want)
        {
            stars.resize (want);
            for (auto& s : stars) respawn (s, false);
        }
        speedNow = (0.05f + 0.50f * level + 0.45f * bass + 1.1f * beat) * motion;
        for (auto& s : stars)
        {
            s.r += dt * speedNow * (0.25f + 1.6f * s.r) * s.speed;
            if (s.r > 1.05f) respawn (s, true);
        }
        if (beatEvent)   // a ring of new stars leaves the middle
            for (int k = 0; k < 28; ++k)
            {
                auto& s = stars[(size_t) rng.nextInt ((int) stars.size())];
                s.angle = (float) k / 28.0f * juce::MathConstants<float>::twoPi;
                s.r = 0.02f;
                s.speed = 1.7f;
            }
    }
    void paintStars (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const auto c = a.getCentre();
        const float diag = 0.5f * std::hypot (a.getWidth(), a.getHeight());
        const float sc = scaleF();
        g.setGradientFill (juce::ColourGradient (pa.withAlpha (0.10f + 0.22f * level + 0.2f * beat), c.x, c.y, pa.withAlpha (0.0f), c.x + diag * 0.6f, c.y, true));
        g.fillEllipse (c.x - diag * 0.6f, c.y - diag * 0.6f, diag * 1.2f, diag * 1.2f);
        for (const auto& s : stars)
        {
            const float tail = std::min (s.r, (0.010f + 0.16f * speedNow * 0.25f) * (0.3f + s.r * 1.3f) * s.speed);
            const juce::Point<float> d (std::cos (s.angle), std::sin (s.angle));
            const auto p1 = c + d * (s.r * diag), p0 = c + d * ((s.r - tail) * diag);
            const juce::Colour cc = s.mix < 0.5f ? pa.interpolatedWith (pb, s.mix * 2.0f) : pb.interpolatedWith (pc, s.mix * 2.0f - 1.0f);
            const float alpha = juce::jlimit (0.0f, 1.0f, s.r * 2.4f) * (0.4f + 0.6f * (0.4f + level));
            g.setColour (cc.brighter (0.25f).withAlpha (alpha));
            juce::Path p;
            p.startNewSubPath (p0);
            p.lineTo (p1);
            g.strokePath (p, juce::PathStrokeType ((0.5f + 2.6f * s.r) * sc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // ---- Horizon: a ridge of the spectrum rolling toward you, a sun on the horizon ------------------------
    static constexpr int kRows = 22;
    void stepHorizon (float dt)
    {
        if (rows.empty()) { rows.assign ((size_t) kRows, view); scroll = 0; }
        scroll += dt * (0.5f + 2.2f * level + 1.4f * beat) * motion * 1.3f;
        while (scroll >= 1.0f)
        {
            scroll -= 1.0f;
            rows.pop_front();
            rows.push_back (view);
        }
    }
    void paintHorizon (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const float w = a.getWidth(), h = a.getHeight(), cx = a.getCentreX(), sc = scaleF();
        const float horizonY = a.getY() + h * 0.46f;
        const juce::Colour dark = bg.interpolatedWith (juce::Colours::black, 0.35f);
        // sky glow and sun
        g.setGradientFill (juce::ColourGradient (pb.withAlpha (0.0f), 0, a.getY(), pa.withAlpha (0.20f + 0.15f * beat), 0, horizonY, false));
        g.fillRect (a.getX(), a.getY(), w, horizonY - a.getY());
        const float sr = std::min (w, h) * (0.24f + 0.012f * beat + 0.02f * bass);
        const juce::Point<float> sun (cx, horizonY - sr * 0.30f);
        {
            juce::Graphics::ScopedSaveState keep (g);
            juce::Path disc;
            disc.addEllipse (sun.x - sr, sun.y - sr, sr * 2, sr * 2);
            g.reduceClipRegion (disc);
            g.setGradientFill (juce::ColourGradient (pb.brighter (0.25f), 0, sun.y - sr, pa.darker (0.1f), 0, sun.y + sr, false));
            g.fillPath (disc);
            g.setColour (dark.withAlpha (0.92f));
            for (int k = 0; k < 6; ++k)
                g.fillRect (sun.x - sr, sun.y + sr * (0.06f + 0.16f * (float) k), sr * 2, (1.0f + 1.1f * (float) k) * sc);
        }
        g.setColour (pa.withAlpha (0.55f));
        g.fillRect (a.getX(), horizonY, w, std::max (1.0f, 1.2f * sc));
        g.setColour (dark);
        g.fillRect (a.getX(), horizonY + 1.0f, w, a.getBottom() - horizonY);
        // the ridges, far to near; every ridge hides the ones behind it
        const int pts = 2 * kBands - 1;
        for (int i = (int) rows.size() - 1; i >= 0; --i)
        {
            const float zf = ((float) i + 1.0f - scroll) / (float) kRows;                // 1 far .. 0 near
            const float closeness = 1.0f - zf;
            const float yRow = horizonY + (a.getBottom() - horizonY + 6.0f) * std::pow (closeness, 1.9f);
            const float spread = w * 0.5f * (0.28f + 1.2f * closeness);
            juce::Path line;
            std::vector<juce::Point<float>> pp;
            pp.reserve ((size_t) pts);
            for (int j = 0; j < pts; ++j)
            {
                const float p = (float) j / (float) (pts - 1) * 2.0f - 1.0f;
                const float pos = std::abs (p) * (float) (kBands - 1);
                const int b0 = (int) pos, b1 = std::min (kBands - 1, b0 + 1);
                const float fr = pos - (float) b0;
                const float v = rows[(size_t) i][(size_t) b0] * (1 - fr) + rows[(size_t) i][(size_t) b1] * fr;
                pp.emplace_back (cx + p * spread, yRow - v * h * 0.40f * std::pow (closeness, 0.95f));
            }
            for (size_t j = 0; j < pp.size(); ++j) { if (j == 0) line.startNewSubPath (pp[j]); else line.lineTo (pp[j]); }
            juce::Path fill (line);
            fill.lineTo (cx + spread, a.getBottom() + 4.0f);
            fill.lineTo (cx - spread, a.getBottom() + 4.0f);
            fill.closeSubPath();
            g.setColour (dark.interpolatedWith (pa, 0.05f).withAlpha (0.97f));
            g.fillPath (fill);
            g.setColour (pb.interpolatedWith (pa, closeness).withAlpha (0.18f + 0.82f * closeness));
            g.strokePath (line, juce::PathStrokeType ((0.8f + 2.2f * closeness) * sc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // ---- the buttons on top of the picture: icons only, always there, brighter near the mouse ----------
    void toggleFollow()
    {
        follow = ! follow;
        if (onFollow) onFollow (follow);
    }
    void layoutControls()
    {
        const auto a = getLocalBounds().toFloat();
        const float s = full ? 56.0f : 40.0f, gap = full ? 12.0f : 8.0f, edge = full ? 20.0f : 10.0f;
        cornerR = juce::Rectangle<float> (a.getRight() - s - edge, a.getY() + edge, s, s);
        const float by = a.getBottom() - s - edge, cx = a.getCentreX();
        prevR = juce::Rectangle<float> (cx - s * 1.6f, by, s, s);
        nextR = juce::Rectangle<float> (cx + s * 0.6f, by, s, s);
        autoR = juce::Rectangle<float> (a.getX() + edge, by, s, s);
        paletteR = autoR.translated (s + gap, 0);
        const float lw = full ? 260.0f : 150.0f;
        leadR = juce::Rectangle<float> (a.getRight() - edge - lw, by, lw, s);
    }
    // "words earlier": how many seconds before they are sung the words light up, to have time to read them (0..2 s, 0.05 steps)
    juce::Rectangle<float> leadTrack() const { return leadR.withTrimmedTop (leadR.getHeight() * 0.55f).reduced (8.0f, 0.0f); }
    bool leadShown() const { return style == VizStyle::Song; }
    void setLeadFrom (float x)
    {
        const auto tr = leadTrack();
        const double v = juce::jlimit (0.0, VizPrefs::kMaxLead, (double) ((x - tr.getX()) / tr.getWidth()) * VizPrefs::kMaxLead);
        VizPrefs::get().lyricLead = std::round (v / 0.05) * 0.05;
    }
    void paintLead (juce::Graphics& g, float al)
    {
        if (! leadShown()) return;
        const bool hot = leadR.contains (mouse) || leadDrag;
        const float k = hot ? 1.0f : al;
        const double v = VizPrefs::get().lyricLead;
        g.setFont (font (full ? 15.0f : 11.5f, 600));
        g.setColour (juce::Colours::white.withAlpha (0.85f * k));
        g.drawText (L ("слова раньше на ", "words early by ") + juce::String (v, 2) + L (" с", " s"),
                    leadR.withHeight (leadR.getHeight() * 0.5f), juce::Justification::centredRight);
        const auto tr = leadTrack();
        const float cy = tr.getCentreY(), x0 = tr.getX(), x1 = tr.getRight();
        const float kx = x0 + (float) (v / VizPrefs::kMaxLead) * (x1 - x0);
        g.setColour (juce::Colours::white.withAlpha (0.25f * k));
        g.fillRoundedRectangle (x0, cy - 2.0f, x1 - x0, 4.0f, 2.0f);
        g.setColour (pa.brighter (0.4f).withAlpha (0.9f * k));
        g.fillRoundedRectangle (x0, cy - 2.0f, kx - x0, 4.0f, 2.0f);
        const float r = full ? 9.0f : 7.0f;
        g.setColour (juce::Colours::white.withAlpha (0.95f * k));
        g.fillEllipse (kx - r, cy - r, r * 2.0f, r * 2.0f);
    }
    void paintControls (juce::Graphics& g, juce::Rectangle<float> a)
    {
        if (cornerR.isEmpty()) layoutControls();
        const float floorA = full ? 0.30f + 0.30f * hover : 0.55f + 0.20f * hover;
        const float al = floorA;
        auto chip = [&] (juce::Rectangle<float> r, bool on)
        {
            const bool hot = r.contains (mouse);
            g.setColour ((hot ? pa.darker (0.6f) : juce::Colours::black).withAlpha ((hot ? 0.82f : 0.52f) * juce::jmax (al, hot ? 0.9f : 0.0f)));
            g.fillEllipse (r);
            g.setColour ((on || hot ? pa.brighter (0.5f) : juce::Colours::white).withAlpha ((on || hot ? 0.95f : 0.40f) * juce::jmax (al, hot ? 0.9f : 0.0f)));
            g.drawEllipse (r.reduced (0.5f), hot ? 2.4f : 1.5f);
            return hot ? 1.0f : al;
        };
        const auto stroke = juce::PathStrokeType (full ? 2.6f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // bigger / close
        {
            const float k = chip (cornerR, false);
            g.setColour (juce::Colours::white.withAlpha (0.92f * k));
            const auto cr = cornerR.reduced (cornerR.getWidth() * 0.32f);
            juce::Path p;
            if (full)
            {
                p.startNewSubPath (cr.getTopLeft()); p.lineTo (cr.getBottomRight());
                p.startNewSubPath (cr.getTopRight()); p.lineTo (cr.getBottomLeft());
            }
            else   // two corner brackets: "make it bigger"
            {
                const float kk = cr.getWidth() * 0.42f;
                p.startNewSubPath (cr.getX(), cr.getY() + kk); p.lineTo (cr.getX(), cr.getY()); p.lineTo (cr.getX() + kk, cr.getY());
                p.startNewSubPath (cr.getRight(), cr.getBottom() - kk); p.lineTo (cr.getRight(), cr.getBottom()); p.lineTo (cr.getRight() - kk, cr.getBottom());
            }
            g.strokePath (p, stroke);
        }
        paintLead (g, al);
        // previous / next picture
        for (int side = 0; side < 2; ++side)
        {
            const auto r = side == 0 ? prevR : nextR;
            const float k = chip (r, false);
            g.setColour (juce::Colours::white.withAlpha (0.92f * k));
            const auto m = r.getCentre();
            const float d = r.getWidth() * 0.13f, dir = side == 0 ? -1.0f : 1.0f;
            juce::Path tri;
            tri.startNewSubPath (m.x - dir * d, m.y - d * 1.5f); tri.lineTo (m.x + dir * d, m.y); tri.lineTo (m.x - dir * d, m.y + d * 1.5f);
            g.strokePath (tri, stroke);
        }
        // dots: which picture, and the name for a moment after a change
        {
            // the row of dots sits ABOVE the two buttons (it used to be squeezed between them and ran under them)
            const float cy = prevR.getY() + (full ? 4.0f : 3.0f);
            for (int i = 0; i < kVizStyles; ++i)
            {
                const float x = a.getCentreX() + ((float) i - (kVizStyles - 1) * 0.5f) * (full ? 15.0f : 10.0f);
                const float dr = (i == (int) style ? 1.3f : 1.0f) * (full ? 4.0f : 3.0f);
                g.setColour (juce::Colours::white.withAlpha ((i == (int) style ? 0.95f : 0.30f) * al));
                g.fillEllipse (x - dr, cy - dr - (full ? 20.0f : 14.0f), dr * 2, dr * 2);
            }
            if (nameFlash > 0.0f)
            {
                g.setFont (font (full ? 22.0f : 16.0f, 600));
                g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, nameFlash)));
                g.drawText (vizStyleName (pending >= 0 ? (VizStyle) pending : style),
                            juce::Rectangle<float> (a.getCentreX() - 120.0f, prevR.getY() - (full ? 74.0f : 54.0f), 240.0f, full ? 34.0f : 26.0f), juce::Justification::centred);
            }
        }
        // automatic change: a circle of arrows
        {
            const float k = chip (autoR, autoCycle);
            g.setColour ((autoCycle ? pa.brighter (0.4f) : juce::Colours::white).withAlpha (0.92f * k));
            const auto c = autoR.getCentre();
            const float rr = autoR.getWidth() * 0.24f;
            juce::Path p;
            p.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.6f, 5.4f, true);
            g.strokePath (p, stroke);
            juce::Path head;
            const float ang = 0.6f;
            const juce::Point<float> tip (c.x + rr * std::sin (ang), c.y - rr * std::cos (ang));
            head.startNewSubPath (tip.x - 5.0f, tip.y - 1.0f); head.lineTo (tip.x + 1.0f, tip.y + 0.5f); head.lineTo (tip.x - 1.0f, tip.y + 6.0f);
            g.strokePath (head, stroke);
        }
        // the colours of the video: three overlapping drops of colour
        {
            const float k = chip (paletteR, follow);
            const auto c = paletteR.getCentre();
            const float dr = paletteR.getWidth() * 0.15f;
            const juce::Colour cols[3] = { pa, pb, pc };
            for (int i = 0; i < 3; ++i)
            {
                const float an = -1.57f + (float) i * 2.094f;
                const juce::Point<float> p (c.x + dr * 0.95f * std::cos (an), c.y + dr * 0.95f * std::sin (an));
                g.setColour ((follow ? cols[i] : juce::Colours::white).withAlpha ((follow ? 0.95f : 0.45f) * k));
                g.fillEllipse (p.x - dr, p.y - dr, dr * 2, dr * 2);
            }
        }
    }
    // ---- state -----------------------------------------------------------------------------------
    const bool full;
    VizStyle style = VizStyle::Vortex;
    bool follow = true, palSeeded = false;
    juce::Colour palA, palB, palC;
    float nameFlash = 0;
    float neonPhase = 0, neonRot = 0;
    float cBass = 0, cMid = 0, cTreb = 0, cBeat = 0, crystalPhase = 0, crystalRot = 0;   // the calmer clock and levels of "Crystal"
    std::array<float, (size_t) kBands> crystalBands {};
    float eFast = 0, eSlow = 0, sinceSection = 0, sectionTarget = 0, sectionShift = 0;   // "Crystal" follows the parts of the music
    float density = 0, pace = 0.8f, densP = -1.0f, ageP = 0.0f;
    int logTick = 0;
    uint32_t onSeqSeen = 0;
    ClipLine clipLine;                                 // the "Clip" picture
    ClipOnsets clipOnsets;                             // where its sounds start
    float clipLogT = 0, clipLogMax = 0; int clipLogOn = 0;
    ClipLine::Output clipOut;
    uint32_t clipOnSeq = 0;
    float clipDemoT = 0.0f;
    int clipLineSeen = -1;
    bool chorusPassed = false;
    std::vector<std::pair<int, int>> lineShapes;    // the drawings of every line of the words (up to two: a scene)
    std::vector<int> lineAct, lineStave, lineHue, lineWorld, lineBurst;   // the part of the song each line is in (see findLineShapes)
    float clipSinceVoice = 99.0f;
    int songMotif = -1; juce::String songInitials;   // the image of the song, and the letters it ends on
    float onEnv = 0, onTarget = 0, onHiEnv = 0, onSeed = 0, popEnv = 0;                                  // sharp sounds: strength now, hissiness, a changing number, and a pop for the text
    float sv[10] {}, emX = -0.5f, emY = 0.0f;                                              // the eased voice numbers of "Voice" and where it is drawn                                                       // ... and how busy it is
    std::array<float, (size_t) kBands> prevView {};
    // the words of the "Song" picture
    std::shared_ptr<LyricsData> song;                  // null = none (yet)
    int lyricsState = 0, fetchGen = 0;                 // 0 not asked, 1 looking, 2 finished
    int lyricsTries = 0;                               // asked again after a busy server this many times
    double lyricsRetryAt = 0.0;                        // when to ask again (millisecond counter), 0 = no
    juce::String songKey, trackArtist, trackTitle;
    double trackPos = 0, trackDur = 0, trackStamp = 0, trackSince = 0;
    bool trackPlaying = false, trackTimeline = false;
    float lyricScroll = 0.0f, chorusMix = 0.0f, moodE = 0.0f, moodHue = 0.62f;
    std::deque<float> gaps;                                                               // seconds between detected beats
    float lastBeatT = -1.0f, period = 0.5f, phi = 0.0f, confS = 0.0f;                    // the tracked pulse
    bool gpuPending = false;
    std::unique_ptr<NebulaRenderer> nebula = std::make_unique<NebulaRenderer>();
    juce::OpenGLContext gl;   // declared after the renderer so that it is closed first
    int pending = -1;
    bool autoCycle = false, hoverTarget = false;
    float veil = 0, hover = 0, sinceSwitch = 0;
    uint32_t lastMove = 0;
    juce::Rectangle<float> cornerR, prevR, nextR, autoR, paletteR, leadR;
    bool leadDrag = false;          // the "words earlier" slider is being dragged
    int heardTick = 0;              // frames, to look for the listener's work once a second
    juce::Time heardStamp;          // the listener's file last taken
    juce::Point<float> mouse { -1.0f, -1.0f };

    std::array<float, (size_t) kBands> bandsDb {}, bar {}, view {};
    Kind kind = Kind::Music;
    bool live = false, beatEvent = false;
    float t = 0, peakEnv = 0.5f, idle = 1.0f, bass = 0, mid = 0, treble = 0, level = 0, centroid = 0.45f;
    float bassAvg = 0, bassVar = 0, sinceBeat = 1.0f, beat = 0, motion = 1.0f, baseHue = 0.7f, speedNow = 0, scroll = 0;
    juce::Colour pa = col::accent, pb = col::accent2, pc = col::accent, bg = col::bg;

    juce::Image fbA, fbB;
    std::vector<Shock> shocks;
    std::vector<Star> stars;
    std::deque<std::array<float, (size_t) kBands>> rows;
    juce::Random rng { 1234 };
};
} // namespace rv::ui
