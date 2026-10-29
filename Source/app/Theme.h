#pragma once
// Look of the app: colours, the Onest typeface, mode colours and names, icons, and the
// Russian/English text switch (Russian unless Windows speaks another language).
#include "../brain/TitleRules.h"
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace rv::ui
{
namespace col
{
const juce::Colour bg { 0xff0c0e12 }, card { 0xff14171d }, cardHi { 0xff1d2129 }, line { 0xff2a2f3a };
const juce::Colour text { 0xfff4f6f9 }, dim { 0xffb0b7c4 }, faint { 0xff7b8391 };
const juce::Colour accent { 0xffffa14d }, accent2 { 0xffff6846 }, ok { 0xff46d08b }, warn { 0xffffc457 };
} // namespace col

inline juce::Colour kindColour (Kind k)
{
    switch (k)
    {
        case Kind::Music: return juce::Colour (0xffa38dff);
        case Kind::Speech: return juce::Colour (0xff43d0ad);
        case Kind::Movie: return juce::Colour (0xffff7a90);
        case Kind::Game: return juce::Colour (0xff5eb2ff);
        default: return col::accent;
    }
}

// Interface language. Chosen in the More menu; "automatic" follows the Windows display language
// (or, on English Windows, a Russian/Ukrainian regional format). The environment variable
// ROVNO_LANG forces one. English and Russian are written at the call site, L (ru, en); the other
// twelve languages come from tools/translations.py, keyed by the English text.
enum class Lang { En, Ru, Et, Uk, De, Fr, Es, It, Pt, Pl, Nl, Sv, Fi, Tr };
constexpr int kLangCount = 14;
Lang language();
void setLanguage (Lang);
juce::String langCode (Lang);          // "en", "ru", "et", ...
juce::String langName (Lang);          // the language's own name: "Eesti", "Deutsch", ...
juce::String languageChoice();         // saved choice: a code, or "" for automatic
void saveLanguageChoice (const juce::String& codeOrEmpty);
void chooseLanguage (Lang);            // switch now and remember
void useAutomaticLanguage();           // forget the choice, follow Windows
inline std::function<void()>& languageListener() { static std::function<void()> f; return f; }   // window title, tray tooltip
juce::String translate (const char* englishUtf8);   // English text -> current language (or itself)

inline juce::String L (const char* ruUtf8, const char* enUtf8)
{
    switch (language())
    {
        case Lang::Ru: return juce::String::fromUTF8 (ruUtf8);
        case Lang::En: return juce::String::fromUTF8 (enUtf8);
        default: return translate (enUtf8);
    }
}
inline juce::String appName() { return language() == Lang::Ru ? juce::String::fromUTF8 ("Ровно") : juce::String ("Evenly"); }

inline juce::String kindName (Kind k)
{
    switch (k)
    {
        case Kind::Music: return L ("Музыка", "Music");
        case Kind::Speech: return L ("Речь", "Speech");
        case Kind::Movie: return L ("Кино", "Movies");
        case Kind::Game: return L ("Игры", "Games");
        default: return L ("Авто", "Auto");
    }
}

struct Fonts
{
    juce::Typeface::Ptr regular, medium, semibold, bold;
    static Fonts& get()
    {
        static Fonts f;
        return f;
    }
private:
    Fonts()
    {
        regular = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestRegular_ttf, BinaryData::OnestRegular_ttfSize);
        medium = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestMedium_ttf, BinaryData::OnestMedium_ttfSize);
        semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestSemiBold_ttf, BinaryData::OnestSemiBold_ttfSize);
        bold = juce::Typeface::createSystemTypefaceFor (BinaryData::OnestBold_ttf, BinaryData::OnestBold_ttfSize);
    }
};

// weight: 400 regular, 500 medium, 600 semibold, 700 bold
inline juce::Font font (float height, int weight = 400)
{
    auto& f = Fonts::get();
    auto tf = weight >= 700 ? f.bold : weight >= 600 ? f.semibold : weight >= 500 ? f.medium : f.regular;
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

// Mode icons, drawn into `r`.
inline void drawKindIcon (juce::Graphics& g, Kind k, juce::Rectangle<float> r, juce::Colour c, float stroke = 2.0f)
{
    g.setColour (c);
    juce::Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    switch (k)
    {
        case Kind::Music:   // a pair of notes
            p.addEllipse (x + w * 0.10f, y + h * 0.66f, w * 0.30f, h * 0.24f);
            p.addEllipse (x + w * 0.56f, y + h * 0.56f, w * 0.30f, h * 0.24f);
            g.fillPath (p);
            g.drawLine (x + w * 0.38f, y + h * 0.78f, x + w * 0.38f, y + h * 0.16f, stroke);
            g.drawLine (x + w * 0.84f, y + h * 0.68f, x + w * 0.84f, y + h * 0.08f, stroke);
            g.drawLine (x + w * 0.38f, y + h * 0.16f, x + w * 0.84f, y + h * 0.08f, stroke * 1.6f);
            break;
        case Kind::Speech:  // a speech bubble: one closed outline with the tail, two lines of text inside
        {
            const float L = x + w * 0.08f, R = x + w * 0.92f, T = y + h * 0.14f, B = y + h * 0.68f, r = w * 0.15f;
            juce::Path b;
            b.startNewSubPath (L + r, T);
            b.lineTo (R - r, T);
            b.quadraticTo (R, T, R, T + r);
            b.lineTo (R, B - r);
            b.quadraticTo (R, B, R - r, B);
            b.lineTo (x + w * 0.52f, B);               // the tail: down to the left and back to the edge
            b.lineTo (x + w * 0.26f, y + h * 0.90f);
            b.lineTo (x + w * 0.30f, B);
            b.lineTo (L + r, B);
            b.quadraticTo (L, B, L, B - r);
            b.lineTo (L, T + r);
            b.quadraticTo (L, T, L + r, T);
            b.closeSubPath();
            g.strokePath (b, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (x + w * 0.28f, y + h * 0.34f, x + w * 0.72f, y + h * 0.34f, stroke);
            g.drawLine (x + w * 0.28f, y + h * 0.49f, x + w * 0.58f, y + h * 0.49f, stroke);
            break;
        }
        case Kind::Movie:   // a film frame with perforations
        {
            g.drawRoundedRectangle (x + w * 0.08f, y + h * 0.16f, w * 0.84f, h * 0.68f, w * 0.10f, stroke);
            for (int i = 0; i < 3; ++i)
            {
                g.fillRoundedRectangle (x + w * (0.18f + i * 0.24f), y + h * 0.24f, w * 0.12f, h * 0.09f, 1.5f);
                g.fillRoundedRectangle (x + w * (0.18f + i * 0.24f), y + h * 0.67f, w * 0.12f, h * 0.09f, 1.5f);
            }
            juce::Path t;
            t.addTriangle (x + w * 0.42f, y + h * 0.39f, x + w * 0.42f, y + h * 0.61f, x + w * 0.62f, y + h * 0.50f);
            g.fillPath (t);
            break;
        }
        case Kind::Game:    // a gamepad
        {
            juce::Path b;
            b.addRoundedRectangle (x + w * 0.04f, y + h * 0.26f, w * 0.92f, h * 0.50f, h * 0.24f);
            g.strokePath (b, juce::PathStrokeType (stroke));
            g.drawLine (x + w * 0.20f, y + h * 0.51f, x + w * 0.40f, y + h * 0.51f, stroke);
            g.drawLine (x + w * 0.30f, y + h * 0.41f, x + w * 0.30f, y + h * 0.61f, stroke);
            g.fillEllipse (x + w * 0.62f, y + h * 0.40f, w * 0.09f, w * 0.09f);
            g.fillEllipse (x + w * 0.74f, y + h * 0.52f, w * 0.09f, w * 0.09f);
            break;
        }
        default:            // auto: level bars evened out (like the app icon)
        {
            const float heights[5] = { 0.30f, 0.62f, 0.44f, 0.44f, 0.44f };
            const float bw = w * 0.12f, gap = w * 0.075f, total = 5 * bw + 4 * gap;
            float bx = x + (w - total) / 2;
            for (int i = 0; i < 5; ++i)
            {
                const float bh = h * heights[i];
                g.setColour (i < 2 ? c.withAlpha (0.45f) : c);
                g.fillRoundedRectangle (bx, y + (h - bh) / 2, bw, bh, bw / 2);
                bx += bw + gap;
            }
            break;
        }
    }
}

// Animation clock: a drop-in for juce::Timer that is called once per vertical blank of the display the
// component is on (100 Hz monitor: 100 frames a second) and tells how long the last frame took, so that
// every motion is computed from real time and looks the same at any refresh rate. Nothing runs while
// stopped, and nothing at all while the window is hidden.
class FrameTimer
{
public:
    explicit FrameTimer (juce::Component* owner) : attachment (owner, [this] { frame(); }) {}
    virtual ~FrameTimer() = default;
    void startTimerHz (int) { if (! running) { running = true; last = now(); lastDt = 1.0f / 60.0f; } }
    void stopTimer() { running = false; }
    bool isTimerRunning() const { return running; }
    float dt() const { return lastDt; }
protected:
    virtual void timerCallback() = 0;
private:
    static double now() { return juce::Time::getMillisecondCounterHiRes(); }
    void frame()
    {
        if (! running) return;
        const double n = now();
        lastDt = juce::jlimit (0.001f, 0.05f, (float) ((n - last) / 1000.0));
        last = n;
        timerCallback();
    }
    juce::VBlankAttachment attachment;
    bool running = false;
    double last = 0;
    float lastDt = 1.0f / 60.0f;
};
inline float rate (float dtSeconds, float perSecond) { return 1.0f - std::exp (-dtSeconds * perSecond); }

// The colour of the mode in use; panels pick it up on their top edge (set by the main window).
inline juce::Colour& neoTint() { static juce::Colour c = col::accent; return c; }

// Neon glow: the same path stroked wide and faint, then narrower, then the line itself.
inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow = 1.0f)
{
    g.setColour (c.withAlpha (0.07f * glow)); g.strokePath (p, juce::PathStrokeType (width + 9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.12f * glow)); g.strokePath (p, juce::PathStrokeType (width + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c); g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A soft halo around a path from four strokes of growing width (cheap enough to repaint every frame, unlike a
// blurred DropShadow). Draw the opaque interior of the shape over it.
inline void softGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float alpha, float spread = 10.0f)
{
    const float widths[4] = { 2.0f, 1.4f, 0.9f, 0.5f }, alphas[4] = { 0.05f, 0.07f, 0.10f, 0.14f };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, alpha * alphas[i] / 0.3f)));
        g.strokePath (p, juce::PathStrokeType (spread * widths[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

inline void glowDot (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float strength = 1.0f)
{
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f * strength), c.x, c.y, colour.withAlpha (0.0f), c.x + radius * 2.6f, c.y, true));
    g.fillEllipse (c.x - radius * 2.6f, c.y - radius * 2.6f, radius * 5.2f, radius * 5.2f);
}

// True while the main window paints baked glass panes: drawCard then adds only the lit top edge.
inline bool& glassOn() { static bool on = false; return on; }

// A glass panel: translucent, a hairline all round, and a thin lit edge on top that takes the
// colour of the current mode and fades out toward the sides.
inline void drawCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = col::card, float radius = 14.0f)
{
    if (! glassOn())
    {
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.05f).withAlpha (0.78f), 0, r.getY(), fill.darker (0.2f).withAlpha (0.70f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.075f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }
    else
    {
        g.setColour (neoTint().withAlpha (0.06f));   // the colour of the mode, faintly, through the glass
        g.fillRoundedRectangle (r.reduced (1.0f), radius);
    }
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    juce::ColourGradient edge (neoTint().withAlpha (0.0f), r.getX() + radius, r.getY(), neoTint().withAlpha (0.0f), r.getRight() - radius, r.getY(), false);
    edge.addColour (0.18, neoTint().withAlpha (0.55f));
    edge.addColour (0.5, neoTint().withAlpha (0.12f));
    g.setGradientFill (edge);
    g.fillRect (r.getX() + radius, r.getY(), r.getWidth() - radius * 2, 1.2f);
}

inline float easeOut (float t) { t = juce::jlimit (0.0f, 1.0f, t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
} // namespace rv::ui
