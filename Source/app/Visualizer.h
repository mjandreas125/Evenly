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
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace rv::ui
{
enum class VizStyle { Neon = 0, Vortex, Flow, Pulse, Aurora, Stars, Horizon };
constexpr int kVizStyles = 7;
inline bool onTheGpu (VizStyle s) { return s == VizStyle::Neon || s == VizStyle::Vortex; }

inline juce::String vizStyleName (VizStyle s)
{
    switch (s)
    {
        case VizStyle::Neon: return L ("Неон", "Neon");
        case VizStyle::Vortex: return L ("Вихрь", "Vortex");
        case VizStyle::Flow: return L ("Поток", "Flow");
        case VizStyle::Pulse: return L ("Пульс", "Pulse");
        case VizStyle::Aurora: return L ("Сияние", "Aurora");
        case VizStyle::Stars: return L ("Звёзды", "Stars");
        default: return L ("Горизонт", "Horizon");
    }
}

// What the user chose, kept between runs (%APPDATA%\Rovno\visual.txt: "visual style auto").
struct VizPrefs
{
    bool visual = false;                 // the main window shows the visual instead of the spectrum
    VizStyle style = VizStyle::Neon;
    bool autoCycle = false;              // change the style now and then

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
        f.replaceWithText (juce::String (visual ? 1 : 0) + " " + juce::String ((int) style) + " " + juce::String (autoCycle ? 1 : 0));
    }
    static VizPrefs load()
    {
        VizPrefs p;
        const auto t = juce::StringArray::fromTokens (file().loadFileAsString().trim(), " ", "");
        if (t.size() >= 3)
        {
            p.visual = t[0].getIntValue() != 0;
            p.style = (VizStyle) juce::jlimit (0, kVizStyles - 1, t[1].getIntValue());
            p.autoCycle = t[2].getIntValue() != 0;
        }
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
        bg = col::bg;
    }

    ~Visualizer() override { gl.detach(); }

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
            case VizStyle::Vortex: if (! onGpu) paintAurora (g, area); break;   // the same colours if there is no GPU picture
            case VizStyle::Flow: paintFlow (g, area); break;
            case VizStyle::Pulse: paintPulse (g, area); break;
            case VizStyle::Aurora: paintAurora (g, area); break;
            case VizStyle::Stars: paintStars (g, area); break;
            default: paintHorizon (g, area); break;
        }
        if (veil > 0.001f) { g.setColour (bg.interpolatedWith (juce::Colours::black, 0.35f).withAlpha (veil)); g.fillRect (area); }
        paintControls (g, area);
    }

    void resized() override { layoutControls(); }

    // ---- mouse and keys ----------------------------------------------------------------------------
    void mouseEnter (const juce::MouseEvent&) override { poke(); }
    void mouseMove (const juce::MouseEvent&) override { poke(); }
    void mouseExit (const juce::MouseEvent&) override { hoverTarget = false; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const auto p = e.position;
        if (cornerR.contains (p)) { if (full) { if (onClose) onClose(); } else if (onExpand) onExpand(); return; }
        if (prevR.contains (p)) { step (-1); return; }
        if (nextR.contains (p)) { step (+1); return; }
        if (autoR.contains (p)) { autoCycle = ! autoCycle; sinceSwitch = 0; if (onChoice) onChoice (style, autoCycle); return; }
        step (+1);   // a click on the picture itself: the next one, like the old players
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (cornerR.contains (e.position) || prevR.contains (e.position) || nextR.contains (e.position) || autoR.contains (e.position)) return;
        if (full) { if (onClose) onClose(); } else if (onExpand) onExpand();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        if (k == juce::KeyPress::rightKey || k == juce::KeyPress::spaceKey || k == juce::KeyPress::downKey) { step (+1); return true; }
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::upKey) { step (-1); return true; }
        if (k.getTextCharacter() == 'a' || k.getTextCharacter() == 'A') { autoCycle = ! autoCycle; sinceSwitch = 0; if (onChoice) onChoice (style, autoCycle); return true; }
        const int digit = k.getTextCharacter() - '1';
        if (digit >= 0 && digit < kVizStyles) { requestStyle ((VizStyle) digit); if (onChoice) onChoice ((VizStyle) digit, autoCycle); return true; }
        return false;
    }

private:
    // ---- plumbing ------------------------------------------------------------------------------
    void timerCallback() override
    {
        if (! isShowing()) { syncGpu(); return; }
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
        f.mode = style == VizStyle::Vortex ? 1 : 0;
        f.dt = dt;
        f.time = t; f.phase = neonPhase; f.rot = neonRot;
        f.bass = bass; f.mid = mid; f.treble = treble; f.beat = beat; f.level = level; f.idle = idle;
        auto rgb = [] (float* o, juce::Colour c) { o[0] = c.getFloatRed(); o[1] = c.getFloatGreen(); o[2] = c.getFloatBlue(); };
        rgb (f.colA, pa); rgb (f.colB, pb); rgb (f.colC, pc);
        for (int i = 0; i < kBands; ++i) f.bands[i] = view[(size_t) i];
        nebula->set (f);
    }
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
            bar[i] += (v - bar[i]) * rate (dt, v > bar[i] ? 34.0f : fall);
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
        const float wantHue = kindColour (kind).getHue();
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
    }
    static float wrap (float h) { return h - std::floor (h); }
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

    // ---- the buttons on top of the picture (visible while the mouse is over it) ------------------------
    void layoutControls()
    {
        const auto a = getLocalBounds().toFloat();
        const float s = full ? 64.0f : 44.0f;
        cornerR = juce::Rectangle<float> (a.getRight() - s - 12, a.getY() + 12, s, s);
        const float by = a.getBottom() - s - 24, cx = a.getCentreX();
        const float nameW = full ? 300.0f : 190.0f;
        prevR = juce::Rectangle<float> (cx - nameW * 0.5f - s - 4, by, s, s);
        nextR = juce::Rectangle<float> (cx + nameW * 0.5f + 4, by, s, s);
        autoR = juce::Rectangle<float> (a.getX() + 8, by, full ? 130.0f : 90.0f, s);
    }
    void paintControls (juce::Graphics& g, juce::Rectangle<float> a)
    {
        if (hover < 0.01f) return;
        if (cornerR.isEmpty()) layoutControls();
        const float al = hover;
        auto chip = [&] (juce::Rectangle<float> r, bool on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f * al));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour ((on ? pa : juce::Colours::white).withAlpha ((on ? 0.8f : 0.22f) * al));
            g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
        };
        const juce::Colour ink = juce::Colours::white.withAlpha (0.92f * al);
        chip (cornerR, false);
        g.setColour (ink);
        const auto cr = cornerR.reduced (cornerR.getWidth() * 0.32f);
        juce::Path p;
        if (full)
        {
            p.startNewSubPath (cr.getTopLeft()); p.lineTo (cr.getBottomRight());
            p.startNewSubPath (cr.getTopRight()); p.lineTo (cr.getBottomLeft());
        }
        else   // two corner brackets: "make it bigger"
        {
            const float k = cr.getWidth() * 0.42f;
            p.startNewSubPath (cr.getX(), cr.getY() + k); p.lineTo (cr.getX(), cr.getY()); p.lineTo (cr.getX() + k, cr.getY());
            p.startNewSubPath (cr.getRight(), cr.getBottom() - k); p.lineTo (cr.getRight(), cr.getBottom()); p.lineTo (cr.getRight() - k, cr.getBottom());
        }
        g.strokePath (p, juce::PathStrokeType (full ? 2.6f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int side = 0; side < 2; ++side)
        {
            const auto r = side == 0 ? prevR : nextR;
            chip (r, false);
            const auto m = r.getCentre();
            const float d = r.getWidth() * 0.13f, dir = side == 0 ? -1.0f : 1.0f;
            juce::Path tri;
            tri.startNewSubPath (m.x - dir * d, m.y - d * 1.5f); tri.lineTo (m.x + dir * d, m.y); tri.lineTo (m.x - dir * d, m.y + d * 1.5f);
            g.strokePath (tri, juce::PathStrokeType (full ? 2.6f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setFont (font (full ? 24.0f : 17.0f, 600));
        g.setColour (ink);
        const juce::String name = vizStyleName (pending >= 0 ? (VizStyle) pending : style);
        g.drawText (name, juce::Rectangle<float> (prevR.getRight(), prevR.getY(), nextR.getX() - prevR.getRight(), prevR.getHeight()), juce::Justification::centred);
        for (int i = 0; i < kVizStyles; ++i)   // dots under the name
        {
            const float x = a.getCentreX() + ((float) i - (kVizStyles - 1) * 0.5f) * (full ? 18.0f : 13.0f);
            g.setColour (juce::Colours::white.withAlpha ((i == (int) style ? 0.9f : 0.25f) * al));
            { const float dr = full ? 5.0f : 3.5f; g.fillEllipse (x - dr, prevR.getBottom() + 6.0f, dr * 2, dr * 2); }
        }
        chip (autoR, autoCycle);
        g.setFont (font (full ? 21.0f : 15.0f, 600));
        g.setColour (autoCycle ? pa.brighter (0.4f).withAlpha (al) : ink);
        g.drawText (L ("Авто", "Auto"), autoR, juce::Justification::centred);
        if (full)
        {
            g.setFont (font (full ? 17.0f : 13.0f, 500));
            g.setColour (juce::Colours::white.withAlpha (0.45f * al));
            g.drawText (L ("Esc — закрыть   ·   ← →, пробел или щелчок — сменить стиль   ·   1–7 — выбрать   ·   A — авто", "Esc closes   ·   ← →, space or a click changes the style   ·   1–7 picks   ·   A turns auto on"),
                        juce::Rectangle<float> (a.getX() + 16, a.getY() + 18, a.getWidth() - 120, 28), juce::Justification::centredLeft);
        }
    }

    // ---- state -----------------------------------------------------------------------------------
    const bool full;
    VizStyle style = VizStyle::Neon;
    float neonPhase = 0, neonRot = 0;
    bool gpuPending = false;
    std::unique_ptr<NebulaRenderer> nebula = std::make_unique<NebulaRenderer>();
    juce::OpenGLContext gl;   // declared after the renderer so that it is closed first
    int pending = -1;
    bool autoCycle = false, hoverTarget = false;
    float veil = 0, hover = 0, sinceSwitch = 0;
    uint32_t lastMove = 0;
    juce::Rectangle<float> cornerR, prevR, nextR, autoR;

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
