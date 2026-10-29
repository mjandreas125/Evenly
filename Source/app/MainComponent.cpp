#include "MainComponent.h"
#include <windows.h>

namespace rv::ui
{
namespace
{
// Layout, in logical pixels (the window scales the whole thing for big monitors).
constexpr int kTop = 20, kHead = 64, kBannerH = 64, kRow1 = 150, kRow2 = 400, kEqClosed = 60, kEqOpen = 290, kBottom = 24, kGap = 16;
constexpr int kPad = 24;

juce::Colour lerpColour (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, t); }
} // namespace

// ---- look and feel ---------------------------------------------------------------------------
RovnoLookAndFeel::RovnoLookAndFeel()
{
    setDefaultSansSerifTypeface (Fonts::get().regular);
    // not fully opaque on purpose: JUCE makes the menu window opaque (white corners) otherwise;
    // the panel itself is drawn in drawPopupMenuBackground
    setColour (juce::PopupMenu::backgroundColourId, col::cardHi.withAlpha (0.99f));
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::white.withAlpha (0.09f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::cardHi);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::line);
}

void RovnoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const juce::Colour c = s.findColour (juce::Slider::trackColourId);
    float hover = s.isMouseOverOrDragging() ? 1.0f : 0.0f, press = s.isMouseButtonDown() ? 1.0f : 0.0f;
    if (auto* ns = dynamic_cast<NeoSlider*> (&s)) { hover = ns->hoverAmount(); press = ns->pressAmount(); }
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle ((float) x, cy - 1.0f, (float) w, 2.0f, 1.0f);
    if (pos > (float) x + 1.0f)
    {
        juce::Path line;
        line.startNewSubPath ((float) x, cy);
        line.lineTo (pos, cy);
        glowStroke (g, line, c, 2.4f, 1.0f + hover * 0.6f);
    }
    const float r = 6.5f + hover * 1.0f + press * 1.4f;
    if (hover > 0.02f) glowDot (g, { pos, cy }, r, c, 0.25f + hover * 0.35f + press * 0.3f);
    g.setColour (col::bg);
    g.fillEllipse (pos - r, cy - r, r * 2, r * 2);
    g.setColour (c.brighter (0.2f));
    g.drawEllipse (pos - r + 1.0f, cy - r + 1.0f, r * 2 - 2.0f, r * 2 - 2.0f, 2.0f);
}

void RovnoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    const juce::Colour c = primary ? col::accent : neoTint();
    juce::Path p;
    p.addRoundedRectangle (r, 10.0f);
    g.setGradientFill (juce::ColourGradient (c.withAlpha (primary ? 0.30f : (down ? 0.20f : (over ? 0.14f : 0.06f))), 0, r.getY(),
                                             c.withAlpha (primary ? 0.12f : 0.02f), 0, r.getBottom(), false));
    g.fillPath (p);
    g.setColour (primary ? c.withAlpha (0.95f) : (over ? c.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.18f)));
    g.strokePath (p, juce::PathStrokeType (1.2f));
}

void RovnoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    g.setColour (primary ? col::accent.brighter (0.5f) : col::text);
    g.setFont (font (15.0f, 600));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
}

void RovnoLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    g.setGradientFill (juce::ColourGradient (col::cardHi.brighter (0.06f), 0, 0, col::bg.brighter (0.03f), 0, (float) alert.getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRect (alert.getLocalBounds(), 1);

    const auto type = alert.getAlertType();
    if (type != juce::MessageBoxIconType::NoIcon)
    {
        const bool warn = type == juce::MessageBoxIconType::WarningIcon;
        const bool ask = type == juce::MessageBoxIconType::QuestionIcon;
        const juce::Colour c = warn ? col::warn : (ask ? col::accent : col::ok);
        const auto badge = juce::Rectangle<float> (28.0f, (float) textArea.getY() + 2.0f, 44.0f, 44.0f);
        g.setColour (c.withAlpha (0.18f));
        g.fillEllipse (badge);
        g.setColour (c);
        g.drawEllipse (badge.reduced (0.5f), 1.5f);
        juce::Path p;
        const auto mid = badge.getCentre();
        if (warn)
        {
            p.addRoundedRectangle (mid.x - 2.0f, mid.y - 11.0f, 4.0f, 14.0f, 2.0f);
            p.addEllipse (mid.x - 2.5f, mid.y + 6.0f, 5.0f, 5.0f);
            g.fillPath (p);
        }
        else if (ask)
        {
            g.setFont (font (26.0f, 700));
            g.drawText ("?", badge, juce::Justification::centred);
        }
        else   // a tick: it is only ever "done"
        {
            p.startNewSubPath (mid.x - 9, mid.y + 1);
            p.lineTo (mid.x - 3, mid.y + 7);
            p.lineTo (mid.x + 9, mid.y - 7);
            g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    g.setColour (col::text);
    layout.draw (g, textArea.toFloat());
}

void RovnoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    // The same liquid glass as the window: coloured light behind, then tint, sheen, lip and a lit rim.
    const auto panel = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (8.0f);
    juce::Path p;
    p.addRoundedRectangle (panel, 16.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.60f), 16, { 0, 6 }).drawForPath (g, p);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (p);
        g.setColour (juce::Colour (0xff06080b));
        g.fillPath (p);
        auto light = [&] (juce::Point<float> c, float radius, juce::Colour colour, float alpha)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha), c.x, c.y, colour.withAlpha (0.0f), c.x + radius, c.y, true));
            g.fillEllipse (c.x - radius, c.y - radius, radius * 2, radius * 2);
        };
        light ({ panel.getX(), panel.getY() }, (float) w * 0.8f, col::accent, 0.42f);
        light ({ panel.getRight(), panel.getBottom() }, (float) w * 0.8f, juce::Colour (0xffa38dff), 0.38f);
        light ({ panel.getRight(), panel.getY() }, (float) w * 0.5f, juce::Colour (0xff2fd3b3), 0.20f);
    }
    GlassBackdrop::paintFinish (g, panel, 16.0f);
}

void RovnoLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 50; idealHeight = 11; return; }
    idealHeight = 36;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 80;
}

void RovnoLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                          const juce::String&, const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (area.reduced (20, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    // JUCE gives every item the full width of the window; the panel is drawn 10 px inside it
    auto r = area.reduced (12, 2).toFloat();
    const juce::Colour base = textColour ? *textColour : col::text;
    if (isHighlighted && isActive)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 10.0f);
    }
    g.setColour (isActive ? base : base.withAlpha (0.38f));
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (34).withTrimmedRight (12), juce::Justification::centredLeft, true);
    if (isTicked)
    {
        juce::Path t;
        const float cx = r.getX() + 17.0f, cy = r.getCentreY();
        t.startNewSubPath (cx - 6, cy);
        t.lineTo (cx - 1.5f, cy + 4.5f);
        t.lineTo (cx + 6.5f, cy - 5);
        g.setColour (col::accent);
        g.strokePath (t, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (hasSubMenu)
    {
        juce::Path a;
        const float ax = r.getRight() - 16.0f, ay = r.getCentreY();
        a.startNewSubPath (ax - 3, ay - 5); a.lineTo (ax + 3, ay); a.lineTo (ax - 3, ay + 5);
        g.setColour (col::dim);
        g.strokePath (a, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// ---- switch ----------------------------------------------------------------------------------
class Switch : public juce::ToggleButton, private FrameTimer
{
public:
    static constexpr int kGlow = 12;   // place the switch with bounds().expanded (kGlow)
    Switch() : FrameTimer (this) { setClickingTogglesState (true); setMouseCursor (juce::MouseCursor::PointingHandCursor); onStateChange = [this] { startTimerHz (60); }; }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (kGlow);   // the rest of the bounds is for the glow
        const float e = easeOut (pos);
        const juce::Colour c = juce::Colours::white.withAlpha (0.28f).interpolatedWith (onColour, e);
        juce::Path track;
        track.addRoundedRectangle (r, r.getHeight() / 2);
        if (e > 0.02f) softGlow (g, track, onColour, 0.45f * e, 9.0f);
        g.setGradientFill (juce::ColourGradient (onColour.withAlpha (0.30f * e), 0, r.getY(), onColour.withAlpha (0.08f * e), 0, r.getBottom(), false));
        g.fillPath (track);
        g.setColour (over && e < 0.5f ? c.brighter (0.4f) : c);
        g.strokePath (track, juce::PathStrokeType (1.4f));
        const float d = r.getHeight() - 8.0f;
        const float kx = r.getX() + 4.0f + pos * (r.getWidth() - d - 8.0f);
        if (e > 0.02f) glowDot (g, { kx + d / 2, r.getCentreY() }, d / 2, onColour, e * 0.8f);
        g.setColour (juce::Colours::white.withAlpha (0.55f + 0.45f * e));
        g.fillEllipse (kx, r.getY() + 4.0f, d, d);
    }
    void snap() { pos = getToggleState() ? 1.0f : 0.0f; repaint(); }
    juce::Colour onColour = col::accent;
private:
    void timerCallback() override
    {
        const float target = getToggleState() ? 1.0f : 0.0f;
        pos += (target - pos) * rate (dt(), 22.0f);
        if (std::abs (target - pos) < 0.01f) { pos = target; stopTimer(); }
        repaint();
    }
    float pos = 0;
};

// ---- the five modes: one pill glides to the chosen tile ----------------------------------------
class ModeBar : public juce::Component, private FrameTimer
{
public:
    std::function<void (Kind)> onPick;   // Kind::Unknown = auto
    static constexpr int kGlow = 18;   // place the bar with bounds().expanded (kGlow)
    ModeBar() : FrameTimer (this) { setMouseCursor (juce::MouseCursor::PointingHandCursor); startTimerHz (60); }
    void set (bool autoOn, Kind current)
    {
        isAuto = autoOn;
        now = current;
        selIndex = autoOn ? 0 : indexOf (current);
        if (! seeded) { pillX = (float) selIndex; pillColour = kindColour (order()[(size_t) selIndex]); seeded = true; }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto items = order();
        const auto inner = getLocalBounds().toFloat().reduced ((float) kGlow);
        const float w = inner.getWidth() / (float) items.size();
        auto tileR = [&] (size_t i) { return juce::Rectangle<float> (inner.getX() + w * (float) i, inner.getY(), w, inner.getHeight()).reduced (4.0f, 0.0f); };
        for (size_t i = 0; i < items.size(); ++i)
        {
            auto r = tileR (i);
            const bool active = items[i] != Kind::Unknown && now == items[i];   // what is playing in auto mode
            g.setColour (juce::Colours::white.withAlpha (0.025f + hoverA[i] * 0.045f));
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (isAuto && active ? kindColour (items[i]).withAlpha (0.55f) : juce::Colours::white.withAlpha (0.075f + hoverA[i] * 0.06f));
            g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        }
        // the gliding selection: a fill that rises from the bottom, a lit hairline and a glowing base
        {
            auto r = juce::Rectangle<float> (inner.getX() + w * pillX, inner.getY(), w, inner.getHeight()).reduced (4.0f, 0.0f);
            juce::Path p;
            p.addRoundedRectangle (r, 14.0f);
            softGlow (g, p, pillColour, 0.30f, 12.0f);
            g.setColour (col::bg);
            g.fillPath (p);
            g.setGradientFill (juce::ColourGradient (pillColour.withAlpha (0.02f), r.getX(), r.getY(), pillColour.withAlpha (0.20f), r.getX(), r.getBottom(), false));
            g.fillPath (p);
            g.setColour (pillColour.withAlpha (0.75f));
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Kind k = items[i];
            auto r = tileR (i);
            const float nearPill = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (pillX - (float) i));
            const bool active = k != Kind::Unknown && now == k;
            const juce::Colour kc = kindColour (k);
            juce::Colour ic = col::dim.interpolatedWith (kc.brighter (0.15f), nearPill);
            if (isAuto && active && nearPill < 0.5f) ic = kc;
            const float iconSize = 36.0f;
            const auto iconR = juce::Rectangle<float> (r.getCentreX() - iconSize / 2, r.getY() + 28.0f - hoverA[i] * 2.0f, iconSize, iconSize);
            if (nearPill > 0.05f) glowDot (g, iconR.getCentre(), 11.0f, kc, nearPill * 0.55f);
            drawKindIcon (g, k, iconR, ic, 2.0f);
            g.setColour (col::dim.interpolatedWith (col::text, juce::jmax (nearPill, hoverA[i] * 0.7f)));
            g.setFont (font (15.5f, nearPill > 0.5f ? 600 : 500));
            g.drawText (kindName (k), r.withTop (r.getY() + 82.0f).withHeight (24.0f), juce::Justification::centred);
            if (k == Kind::Unknown)
            {
                g.setColour (col::faint);
                g.setFont (font (13.0f));
                g.drawText (L ("само", "automatic"), r.withTop (r.getY() + 104.0f).withHeight (20.0f), juce::Justification::centred);
            }
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { hover = indexAt (e.x); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.x);
        if (i >= 0 && onPick) onPick (order()[(size_t) i]);
    }
private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        bool moving = false;
        const float tp = (float) selIndex;
        if (std::abs (tp - pillX) > 0.002f) { pillX += (tp - pillX) * rate (dt(), 14.0f); moving = true; } else pillX = tp;
        const juce::Colour tc = kindColour (order()[(size_t) selIndex]);
        if (pillColour != tc) { pillColour = lerpColour (pillColour, tc, rate (dt(), 12.0f)); if (pillColour.getARGB() == tc.getARGB()) pillColour = tc; moving = true; }
        for (int i = 0; i < 5; ++i)
        {
            const float t = hover == i ? 1.0f : 0.0f;
            if (std::abs (t - hoverA[(size_t) i]) > 0.01f) { hoverA[(size_t) i] += (t - hoverA[(size_t) i]) * rate (dt(), 16.0f); moving = true; }
        }
        if (moving) repaint();
    }
    static std::vector<Kind> order() { return { Kind::Unknown, Kind::Music, Kind::Speech, Kind::Movie, Kind::Game }; }
    static int indexOf (Kind k) { const auto o = order(); for (size_t i = 0; i < o.size(); ++i) if (o[i] == k) return (int) i; return 0; }
    int indexAt (int x) const { return juce::jlimit (0, 4, (x - kGlow) * 5 / juce::jmax (1, getWidth() - 2 * kGlow)); }
    bool isAuto = true, seeded = false;
    Kind now = Kind::Music;
    int selIndex = 0, hover = -1;
    float pillX = 0;
    std::array<float, 5> hoverA {};
    juce::Colour pillColour = col::accent;
};

// ---- the output chooser: a pill that opens the rounded menu -----------------------------------
class DeviceBox : public juce::Component
{
public:
    std::function<void (int)> onPick;
    DeviceBox() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void set (const juce::StringArray& labels, int selected) { items = labels; sel = selected; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (open ? col::line : (over ? col::cardHi.brighter (0.10f) : col::cardHi));
        g.fillRoundedRectangle (r, r.getHeight() / 2);
        g.setColour (open ? col::accent.withAlpha (0.8f) : (over ? col::faint.withAlpha (0.8f) : col::line));
        g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
        // a small speaker
        const float ix = r.getX() + 20.0f, iy = r.getCentreY();
        juce::Path sp;
        sp.startNewSubPath (ix - 6, iy - 3); sp.lineTo (ix - 2, iy - 3); sp.lineTo (ix + 3, iy - 7);
        sp.lineTo (ix + 3, iy + 7); sp.lineTo (ix - 2, iy + 3); sp.lineTo (ix - 6, iy + 3); sp.closeSubPath();
        g.setColour (col::dim);
        g.fillPath (sp);
        juce::Path wave;
        wave.addCentredArc (ix + 3, iy, 6, 6, 0, 0.9f, 2.2f, true);
        g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::text);
        g.setFont (font (15.0f, 500));
        const juce::String t = sel >= 0 && sel < items.size() ? items[sel] : L ("Выберите выход", "Pick an output");
        g.drawText (t, r.withTrimmedLeft (42).withTrimmedRight (40), juce::Justification::centredLeft, true);
        juce::Path arrow;
        const float ax = r.getRight() - 22.0f, ay = r.getCentreY();
        arrow.startNewSubPath (ax - 5, ay - 2.5f); arrow.lineTo (ax, ay + 2.5f); arrow.lineTo (ax + 5, ay - 2.5f);
        g.setColour (col::dim);
        g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! contains (e.getPosition()) || items.isEmpty()) return;
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        for (int i = 0; i < items.size(); ++i) m.addItem (i + 1, items[i], true, i == sel);
        open = true;
        repaint();
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (40),
                         [safe = juce::Component::SafePointer<DeviceBox> (this)] (int r)
                         {
                             if (! safe) return;
                             safe->open = false;
                             safe->repaint();
                             if (r > 0 && safe->onPick) safe->onPick (r - 1);
                         });
    }
private:
    juce::StringArray items;
    int sel = -1;
    bool over = false, open = false;
};

// ---- the spectrum of what is coming in, third octaves (50 Hz .. 16 kHz) ------------------------
// The Controller rises at once and sinks slowly; this view eases a little further at 60 fps so the
// bars move like a meter, and a thin mark above each bar keeps its recent top for a moment.
class Spectrum : public juce::Component, private FrameTimer
{
public:
    Spectrum() : FrameTimer (this) { startTimerHz (60); }
    void set (const std::array<float, (size_t) kBands>& b, const std::array<float, (size_t) kBands>& p, juce::Colour c, bool on)
    {
        target = b;
        targetPeaks = p;
        colour = c;
        live = on;
        if (! seeded) { shown = b; shownPeaks = p; seeded = true; }   // first picture is the real one
    }
    void paint (juce::Graphics& g) override
    {
        const auto all = getLocalBounds().toFloat();
        const auto r = all.withTrimmedBottom (22.0f);
        // Measured on real material through this engine: a third octave peaks around -6 dB and
        // ordinary content sits between -50 and -10, so this window fills the plot without pinning
        // it to the top.
        const float lo = -66.0f, hi = -6.0f;
        auto yOf = [&] (float db) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo)) * (r.getHeight() - 14.0f) - 1.0f; };
        const float step = r.getWidth() / (float) kBands;
        auto xOf = [&] (int i) { return r.getX() + step * ((float) i + 0.5f); };
        const juce::Colour base = live ? colour : col::dim;

        // a quiet grid, like an analyser: every 20 dB
        for (float db : { -46.0f, -26.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (r.getX(), yOf (db), r.getWidth(), 1.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (r.getX(), r.getBottom() - 0.5f, r.getWidth(), 1.0f);

        // The spectrum as one smooth curve (Catmull-Rom through the third-octave points). The curve starts and ends
        // on the baseline at the very edges, so both ends slope down into nothing instead of being cut off.
        const float baseY = yOf (lo);
        auto curve = [&] (const std::array<float, (size_t) kBands>& v, juce::Path& p)
        {
            std::vector<juce::Point<float>> pts;
            pts.emplace_back (r.getX(), baseY);
            for (int i = 0; i < kBands; ++i) pts.emplace_back (xOf (i), yOf (v[(size_t) i]));
            pts.emplace_back (r.getRight(), baseY);
            const int n = (int) pts.size();
            auto at = [&] (int i) { return pts[(size_t) juce::jlimit (0, n - 1, i)]; };
            p.startNewSubPath (pts.front());
            for (int i = 0; i < n - 1; ++i)
            {
                const auto p0 = at (i - 1), p1 = at (i), p2 = at (i + 1), p3 = at (i + 2);
                p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
            }
        };
        // a stroke whose colour fades to nothing at both ends
        auto strokeFaded = [&] (const juce::Path& path, float width, juce::Colour c, float alphaMul)
        {
            juce::ColourGradient fade (c.withAlpha (0.0f), r.getX(), 0, c.withAlpha (0.0f), r.getRight(), 0, false);
            fade.addColour (0.06, c.withAlpha (alphaMul));
            fade.addColour (0.94, c.withAlpha (alphaMul));
            g.setGradientFill (fade);
            g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        juce::Path line;
        curve (shown, line);
        juce::Path fill (line);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (base.withAlpha (live ? 0.38f : 0.10f), 0, r.getY(), base.withAlpha (0.0f), 0, r.getBottom(), false));
        g.fillPath (fill);
        // the recent top of every band, as a faint thin line
        juce::Path tops;
        curve (shownPeaks, tops);
        strokeFaded (tops, 1.0f, base, live ? 0.24f : 0.08f);
        const juce::Colour lineColour = base.brighter (0.15f);
        strokeFaded (line, 2.0f, lineColour, live ? 0.95f : 0.35f);

        g.setColour (col::faint);
        g.setFont (font (12.5f, 500));
        const char* labels[5] = { "50", "200", "800", "3,2k", "12k" };
        for (int k = 0; k < 5; ++k)
            g.drawText (labels[k], juce::Rectangle<float> (xOf (k * 6) - 24, all.getBottom() - 18, 48, 18), juce::Justification::centred);
    }
private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        bool changed = false;
        for (size_t i = 0; i < (size_t) kBands; ++i)
        {
            const float a = shown[i] + (target[i] - shown[i]) * rate (dt(), 38.0f);
            const float b = shownPeaks[i] + (targetPeaks[i] - shownPeaks[i]) * rate (dt(), 26.0f);
            if (std::abs (a - shown[i]) > 0.02f || std::abs (b - shownPeaks[i]) > 0.02f) changed = true;
            shown[i] = a;
            shownPeaks[i] = b;
        }
        if (changed) repaint();
    }
    std::array<float, (size_t) kBands> target {}, targetPeaks {}, shown {}, shownPeaks {};
    juce::Colour colour = col::accent;
    bool live = true, seeded = false;
public:
    void prime() { shown.fill (-90.0f); shownPeaks.fill (-90.0f); target.fill (-90.0f); targetPeaks.fill (-90.0f); seeded = false; }
};

// ---- loudness in and out over the last 90 s ----------------------------------------------------
class LevelGraph : public juce::Component
{
public:
    void set (const std::deque<std::pair<float, float>>& h, float target, bool on, juce::Colour c)
    {
        hist.assign (h.begin(), h.end());
        tgt = target;
        enabled = on;
        colour = c;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto all = getLocalBounds().toFloat();
        auto r = all.withTrimmedLeft (34.0f).withTrimmedBottom (20.0f).withTrimmedTop (8.0f);
        const float lo = -42.0f, hi = -4.0f;
        auto yOf = [&] (float v) { return r.getBottom() - (juce::jlimit (lo, hi, v) - lo) / (hi - lo) * r.getHeight(); };
        // scale
        g.setFont (font (12.5f, 500));
        for (float db : { -10.0f, -20.0f, -30.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (r.getX(), yOf (db), r.getWidth(), 1.0f);
            g.setColour (col::faint);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (all.getX(), yOf (db) - 9, 28, 18), juce::Justification::centredRight);
        }
        g.setColour (col::faint);
        g.drawText (L ("90 с назад", "90 s ago"), juce::Rectangle<float> (r.getX(), all.getBottom() - 18, 110, 18), juce::Justification::centredLeft);
        g.drawText (L ("сейчас", "now"), juce::Rectangle<float> (r.getRight() - 80, all.getBottom() - 18, 80, 18), juce::Justification::centredRight);
        // target line
        const float ty = yOf (tgt);
        g.setColour (col::dim.withAlpha (0.7f));
        for (float x = r.getX(); x < r.getRight(); x += 9.0f) g.fillRect (x, ty - 0.75f, juce::jmin (4.5f, r.getRight() - x), 1.5f);
        {
            // the label sits in a small pill at the left edge, away from the lines
            g.setFont (font (12.5f, 600));
            const juce::String t = L ("цель", "target");
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 14.0f;
            const float ly = ty - 24 < r.getY() ? ty + 4 : ty - 24;
            const juce::Rectangle<float> pill (r.getX() + 4.0f, ly, w, 20.0f);
            g.setColour (col::card.withAlpha (0.92f));
            g.fillRoundedRectangle (pill, 10.0f);
            g.setColour (col::dim);
            g.drawText (t, pill, juce::Justification::centred);
        }
        if (hist.size() < 2) return;
        const size_t n = 180;
        const float dx = r.getWidth() / (float) (n - 1);
        const size_t start = n - hist.size();
        auto xAt = [&] (size_t i) { return r.getX() + dx * (float) (start + i); };
        // A pause is not a loudness of zero. Anything below the bottom of the chart carries no
        // shape worth drawing, so those stretches become a break in the line: without this, every
        // pause pulls the curve straight down to the floor and the picture is all holes.
        auto onChart = [&] (float v) { return v > lo; };

        // Shade the stretches where nothing came out, so a pause reads as a pause.
        for (size_t i = 0; i < hist.size();)
        {
            if (onChart (hist[i].second)) { ++i; continue; }
            const size_t from = i;
            while (i < hist.size() && ! onChart (hist[i].second)) ++i;
            const float x0 = xAt (from), x1 = xAt (i > 0 ? i - 1 : 0);
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRect (juce::Rectangle<float> (x0, r.getY(), juce::jmax (1.0f, x1 - x0), r.getHeight()));
        }

        const juce::Colour oc = enabled ? colour : col::dim;
        // Every run of sound is drawn on its own; the gaps stay empty.
        auto drawSeries = [&] (bool out)
        {
            for (size_t i = 0; i < hist.size();)
            {
                const auto value = [&] (size_t k) { return out ? hist[k].second : hist[k].first; };
                if (! onChart (value (i))) { ++i; continue; }
                const size_t from = i;
                while (i < hist.size() && onChart (value (i))) ++i;
                if (i - from < 2) continue;
                juce::Path p;
                for (size_t k = from; k < i; ++k)
                {
                    const float x = xAt (k), y = yOf (value (k));
                    if (k == from) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                if (out)
                {
                    juce::Path fill (p);
                    fill.lineTo (xAt (i - 1), r.getBottom());
                    fill.lineTo (xAt (from), r.getBottom());
                    fill.closeSubPath();
                    g.setGradientFill (juce::ColourGradient (oc.withAlpha (0.30f), 0, r.getY(), oc.withAlpha (0.0f), 0, r.getBottom(), false));
                    g.fillPath (fill);
                    g.setColour (oc);
                    g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else
                {
                    g.setColour (col::dim.withAlpha (0.6f));
                    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
                }
            }
        };
        drawSeries (false);
        drawSeries (true);
    }
private:
    std::vector<std::pair<float, float>> hist;
    float tgt = -11;
    bool enabled = true;
    juce::Colour colour = col::accent;
};

// ---- 10-band EQ: drag the points; the dashed line is the automatic tone correction -----------
class EqEditor : public juce::Component
{
public:
    std::function<void (int, float)> onChange;
    void set (const float user[10], const float total[10], const std::array<float, 10>& tone, juce::Colour c)
    {
        std::copy (user, user + 10, u);
        std::copy (total, total + 10, base);
        std::copy (tone.begin(), tone.end(), t);
        colour = c;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = plot();
        g.setFont (font (12.5f, 500));
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            g.setColour (db == 0.0f ? juce::Colours::white.withAlpha (0.16f) : juce::Colours::white.withAlpha (0.06f));
            g.fillRect (r.getX(), yOf (db) - 0.5f, r.getWidth(), 1.0f);
            g.setColour (col::faint);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (0, yOf (db) - 9, 34, 18), juce::Justification::centredRight);
        }
        const char* labels[10] = { "60", "120", "210", "400", "730", "1,4k", "2,5k", "4,7k", "8,6k", "16k" };
        for (int i = 0; i < 10; ++i)
        {
            g.setColour (col::faint);
            g.drawText (labels[i], juce::Rectangle<float> (xOf (i) - 24, r.getBottom() + 8, 48, 18), juce::Justification::centred);
        }
        juce::Path dashed;
        for (int i = 0; i < 10; ++i)
            if (i == 0) dashed.startNewSubPath (xOf (i), yOf (base[i] + t[i])); else dashed.lineTo (xOf (i), yOf (base[i] + t[i]));
        juce::Path dash;
        const float lengths[2] = { 5.0f, 5.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dash, dashed, lengths, 2);
        g.setColour (col::dim.withAlpha (0.75f));
        g.fillPath (dash);
        juce::Path p;
        for (int i = 0; i < 10; ++i)
        {
            const juce::Point<float> pt (xOf (i), yOf (base[i]));
            if (i == 0) p.startNewSubPath (pt);
            else
            {
                const juce::Point<float> prev (xOf (i - 1), yOf (base[i - 1]));
                p.cubicTo (prev.x + (pt.x - prev.x) * 0.5f, prev.y, prev.x + (pt.x - prev.x) * 0.5f, pt.y, pt.x, pt.y);
            }
        }
        juce::Path fill (p);
        fill.lineTo (xOf (9), yOf (0));
        fill.lineTo (xOf (0), yOf (0));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.20f), 0, r.getY(), colour.withAlpha (0.02f), 0, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int i = 0; i < 10; ++i)
        {
            const float rad = i == drag ? 8.5f : 6.5f;
            g.setColour (col::card);
            g.fillEllipse (xOf (i) - rad, yOf (base[i]) - rad, rad * 2, rad * 2);
            g.setColour (std::abs (u[i]) > 0.05f || i == drag ? colour : col::dim);
            g.drawEllipse (xOf (i) - rad, yOf (base[i]) - rad, rad * 2, rad * 2, 2.4f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1;
        float best = 26.0f;
        for (int i = 0; i < 10; ++i) if (std::abs (xOf (i) - (float) e.x) < best) { best = std::abs (xOf (i) - (float) e.x); drag = i; }
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const float v = juce::jlimit (-12.0f, 12.0f, valOf ((float) e.y));
        const float delta = std::round ((v - (base[drag] - u[drag])) * 2.0f) / 2.0f;   // 0.5 dB steps
        if (onChange) onChange (drag, delta);
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (drag >= 0 && onChange) onChange (drag, 0.0f); }
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f).withTrimmedLeft (36.0f).withTrimmedBottom (26.0f); }
    float xOf (int i) const { const auto r = plot(); return r.getX() + 12.0f + (r.getWidth() - 24.0f) * (float) i / 9.0f; }
    float yOf (float db) const { const auto r = plot(); return r.getCentreY() - juce::jlimit (-12.0f, 12.0f, db) / 12.0f * r.getHeight() * 0.5f; }
    float valOf (float y) const { const auto r = plot(); return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * 12.0f; }
    float u[10] {}, base[10] {}, t[10] {};
    juce::Colour colour = col::accent;
    int drag = -1;
};

// ---- the equalizer panel: a bottom sheet that rises over the other cards (the window never grows) ----
class EqPanel : public juce::Component
{
public:
    static constexpr int M = 30;   // room around the sheet for its shadow: the bounds are M bigger on every side
    std::function<void()> onToggle;
    const GlassBackdrop* glass = nullptr;   // the main window's glass, for the pane's own fill
    float anim = 0.0f;            // 0 closed .. 1 open, set by the main component
    bool open = false;
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().reduced (M).toFloat();
        if (glass != nullptr && glass->scene.isValid()) glass->paintLive (g, r, getPosition());
        else { g.setColour (col::bg.brighter (0.035f)); g.fillRoundedRectangle (r, 14.0f); }
        drawCard (g, r, col::card.brighter (0.05f * hoverA));
        const auto head = getLocalBounds().reduced (M).withHeight (kEqClosed);
        g.setColour (col::text);
        g.setFont (font (19.0f, 600));
        g.drawText (L ("Эквалайзер", "Equalizer"), head.reduced (24, 0), juce::Justification::centredLeft);
        const float cx = (float) head.getRight() - 30.0f, cy = (float) head.getCentreY();
        juce::Path ch;
        ch.startNewSubPath (-6, 3); ch.lineTo (0, -3); ch.lineTo (6, 3);   // points up = it will open; down when open
        const float turn = (1.0f - easeOut (anim)) * juce::MathConstants<float>::pi;
        g.setColour (hoverA > 0.5f ? col::text : col::dim);
        g.strokePath (ch, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::rotation (turn).translated (cx, cy));
    }
    bool hitTest (int x, int y) override { return getLocalBounds().reduced (M).contains (x, y); }
    void mouseMove (const juce::MouseEvent& e) override { setHover (e.y < M + kEqClosed); }
    void mouseExit (const juce::MouseEvent&) override { setHover (false); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.y < M + kEqClosed && onToggle) onToggle(); }
    void tickHover (float dtSeconds)
    {
        const float t = hover ? 1.0f : 0.0f;
        hoverA += (t - hoverA) * rate (dtSeconds, 18.0f);
        if (std::abs (t - hoverA) < 0.01f) hoverA = t;
        repaint();
    }
    bool hovering() const { return hover; }
    float hoverAmount() const { return hoverA; }
private:
    void setHover (bool h)
    {
        if (h == hover) return;
        hover = h;
        setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    bool hover = false;
    float hoverA = 0;
};

// Dims everything behind the open panel; a click on it closes the panel.
class Scrim : public juce::Component
{
public:
    std::function<void()> onClick;
    float amount = 0.0f;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.55f * amount)); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
};

// ---- main component --------------------------------------------------------------------------
namespace
{
void styleSlider (juce::Slider& s, juce::Colour c)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (0.0, 1.0, 0.01);
    s.setColour (juce::Slider::trackColourId, c);
    s.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
} // namespace

namespace
{
// A speech-like spectrum and a loudness history for the README pictures (never used in normal running).
void demoFill (std::array<float, (size_t) kBands>& b, std::array<float, (size_t) kBands>& p, std::deque<std::pair<float, float>>& hist)
{
    for (int i = 0; i < kBands; ++i)
    {
        const float x = (float) i;
        const float body = -52.0f + 34.0f * std::exp (-std::pow ((x - 9.0f) / 6.5f, 2.0f)) + 7.0f * std::sin (x * 0.9f) + 4.0f * std::sin (x * 2.1f + 1.0f);
        b[(size_t) i] = body;
        p[(size_t) i] = body + 5.0f + 2.0f * std::sin (x * 1.3f);
    }
    hist.clear();
    for (int k = 0; k < 150; ++k)
        hist.emplace_back (-27.0f + 4.0f * std::sin ((float) k * 0.21f) + 2.0f * std::sin ((float) k * 0.7f),
                           -11.5f + 1.6f * std::sin ((float) k * 0.21f + 0.8f) + 0.8f * std::sin ((float) k * 0.6f));
}
} // namespace

MainComponent::MainComponent (Controller& controller) : c (controller)
{
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::icon256_png, BinaryData::icon256_pngSize);
    power = std::make_unique<Switch>();
    levelSwitch = std::make_unique<Switch>();
    toneSwitch = std::make_unique<Switch>();
    modes = std::make_unique<ModeBar>();
    spectrum = std::make_unique<Spectrum>();
    spectrum->prime();
    graph = std::make_unique<LevelGraph>();
    eq = std::make_unique<EqEditor>();
    deviceBox = std::make_unique<DeviceBox>();
    for (juce::Component* comp : std::initializer_list<juce::Component*> { power.get(), levelSwitch.get(), toneSwitch.get(), modes.get(),
                                                                            spectrum.get(), graph.get(), deviceBox.get(),
                                                                            &volume, &fid, &bass, &surr, &amb, &resetSound, &bannerButton, &moreButton })
        addAndMakeVisible (comp);
    scrim = std::make_unique<Scrim>();
    eqPanel = std::make_unique<EqPanel>();
    addChildComponent (scrim.get());
    addAndMakeVisible (eqPanel.get());           // last: above all the cards
    eqPanel->addChildComponent (eq.get());
    scrim->onClick = [this] { setEqOpen (false, true); };
    eqPanel->onToggle = [this] { setEqOpen (! eqOpen, true); };
    eqPanel->glass = &glass;
    resetSound.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    bannerButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    moreButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    power->onClick = [this] { if (! syncing) c.setEnabled (power->getToggleState()); };
    levelSwitch->onClick = [this] { if (! syncing) c.setLeveler (levelSwitch->getToggleState()); };
    toneSwitch->onClick = [this] { if (! syncing) c.setToneAuto (toneSwitch->getToggleState()); };
    modes->onPick = [this] (Kind k) { if (k == Kind::Unknown) c.setAuto(); else c.pick (k); syncFromController(); };

    styleSlider (volume, col::accent);
    volume.onValueChange = [this] { if (! syncing) c.setVolume ((float) volume.getValue()); repaint (loudR); };
    for (auto* s : { &fid, &bass, &surr, &amb })
    {
        styleSlider (*s, col::accent);
        s->onValueChange = [this] { if (! syncing) soundChanged(); repaint (soundR); };
    }
    resetSound.setButtonText (L ("Сбросить", "Reset"));
    resetSound.onClick = [this] { c.resetTweaks (shownKind); syncFromController(); };
    bannerButton.getProperties().set ("primary", true);
    bannerButton.onClick = [this] { bannerAction(); };
    moreButton.setButtonText (L ("Ещё", "More"));
    moreButton.onClick = [this] { showMoreMenu(); };
    deviceBox->onPick = [this] (int i)
    {
        if (! syncing && i >= 0 && i < deviceGuids.size()) c.selectDevice (deviceGuids[i]);
    };
    eq->onChange = [this] (int band, float delta)
    {
        auto t = c.tweaks (shownKind);
        t.eq[band] = delta;
        c.setTweaks (shownKind, t);
        syncFromController();
    };

    c.addChangeListener (this);
    kcCur = kindColour (c.now().kind);
    setSize (kWidth, preferredHeight());
    syncFromController();
    ticker.tick = [this] { eqTick(); };
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    c.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

MainComponent::Banner MainComponent::banner() const
{
    if (hideBanner) return Banner::None;
    if (! c.now().installed) return Banner::NotInstalled;
    if (c.now().fxSoundInChain) return Banner::FxSound;
    return Banner::None;
}

void MainComponent::syncFromController()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto& st = c.now();
    power->setToggleState (c.enabled(), juce::dontSendNotification);
    levelSwitch->setToggleState (c.leveler(), juce::dontSendNotification);
    toneSwitch->setToggleState (c.toneAuto(), juce::dontSendNotification);
    modes->set (c.autoMode(), st.kind);
    volume.setValue (c.volume(), juce::dontSendNotification);

    shownKind = st.kind;
    const juce::Colour kc = kindColour (shownKind);
    const ModeSound s = c.sound (shownKind);
    fid.setValue (s.fidelity / 127.0, juce::dontSendNotification);
    bass.setValue (s.bass / 127.0, juce::dontSendNotification);
    surr.setValue (s.surround / 127.0, juce::dontSendNotification);
    amb.setValue (s.ambience / 127.0, juce::dontSendNotification);
    for (Switch* sw : { power.get(), levelSwitch.get(), toneSwitch.get() }) sw->snap();
    for (auto* sl : { &fid, &bass, &surr, &amb }) sl->setColour (juce::Slider::trackColourId, kcCur);
    toneSwitch->onColour = kcCur;
    const auto tw = c.tweaks (shownKind);
    eq->set (tw.eq, s.eq, st.tone, kc);
    {
        auto bands = st.bands, peaks = st.bandPeaks;
        auto hist = st.history;
        if (demoData) demoFill (bands, peaks, hist);
        spectrum->set (bands, peaks, kcCur, demoData || (c.enabled() && st.running));
        graph->set (hist, targetLufsFor (c.volume()) + s.targetOffsetDb, c.leveler() && c.enabled(), kcCur);
    }

    deviceGuids.clear();
    juce::StringArray labels;
    int sel = -1;
    for (const auto& d : c.outputs())
    {
        // FxSound's own device is never the right place; sleeping outputs only if already chosen
        // (an unused HDMI port without a monitor shows up as one).
        if (d.isFxSound || (! d.plugged && juce::String (d.guid.c_str()) != c.deviceGuid())) continue;
        deviceGuids.add (d.guid.c_str());
        juce::String label = d.name.c_str();
        if (! d.plugged) label += L ("  (спит)", "  (asleep)");
        else if (! d.rovnoInstalled) label += L ("  (не подключён)", "  (not connected)");
        labels.add (label);
        if (juce::String (d.guid.c_str()) == c.deviceGuid()) sel = deviceGuids.size() - 1;
    }
    deviceBox->set (labels, sel);

    const auto b = banner();
    bannerButton.setVisible (b != Banner::None);
    bannerButton.setButtonText (b == Banner::NotInstalled ? L ("Подключить", "Connect") : L ("Переключить", "Switch"));
    if (getHeight() != preferredHeight()) setSize (kWidth, preferredHeight()); else resized();
    repaint();
}

int MainComponent::preferredHeight() const
{
    // The window has one height: the equalizer opens as a sheet over the cards instead of growing it.
    return kTop + kHead + kGap + (banner() != Banner::None ? kBannerH + kGap : 0) + kRow1 + kGap + kRow2 + kGap + kEqClosed + kBottom;
}

int MainComponent::eqHeight() const
{
    const float t = eqAnim < 0.5f ? 4 * eqAnim * eqAnim * eqAnim : 1 - std::pow (-2 * eqAnim + 2, 3.0f) / 2;   // ease in-out
    return kEqClosed + juce::roundToInt ((float) (kEqOpen - kEqClosed) * t);
}

void MainComponent::setEqOpen (bool open, bool animate)
{
    eqOpen = open;
    if (! animate) eqAnim = open ? 1.0f : 0.0f;
    eqPanel->toFront (false);
    layoutEq();
    if (animate) ticker.startTimerHz (60);
}

// Where the sheet is for the current point of the animation.
void MainComponent::layoutEq()
{
    eqPanel->anim = eqAnim;
    eqPanel->open = eqOpen;
    eqPanel->setBounds (eqR.withTop (eqR.getBottom() - eqHeight()).expanded (EqPanel::M));
    // The dimming is the only thing that repaints the whole window, so it fades in during the first quarter of
    // the movement (and out during the last) and stays put in between.
    const float dim = easeOut (juce::jmin (1.0f, eqAnim * 4.0f));
    const bool dimVisible = eqAnim > 0.005f;
    if (scrim->isVisible() != dimVisible) scrim->setVisible (dimVisible);
    if (scrim->getBounds() != getLocalBounds()) scrim->setBounds (getLocalBounds());
    if (std::abs (scrim->amount - dim) > 0.002f) { scrim->amount = dim; scrim->repaint(); }
    // the plot appears once the sheet has some height; no alpha layer (it would be redrawn through a temporary image)
    const bool plotShown = eqAnim > 0.22f;
    if (eq->isVisible() != plotShown) eq->setVisible (plotShown);
    if (plotShown) eq->setBounds (eqPanel->getLocalBounds().reduced (EqPanel::M).withTrimmedTop (kEqClosed - 4).reduced (16, 10));
}

// One step of the sheet rising or sinking (60 Hz, only while it moves).
void MainComponent::eqTick()
{
    const float target = eqOpen ? 1.0f : 0.0f;
    const float speed = ticker.frameDt() / 0.34f;   // 0.34 s end to end, whatever the refresh rate
    eqAnim = eqAnim < target ? std::min (target, eqAnim + speed) : std::max (target, eqAnim - speed);
    eqPanel->tickHover (ticker.frameDt());
    const bool hoverSettled = std::abs ((eqPanel->hovering() ? 1.0f : 0.0f) - eqPanel->hoverAmount()) < 0.01f;
    if (eqAnim == target && hoverSettled) ticker.stopTimer();
    layoutEq();
}

void MainComponent::soundChanged()
{
    ModeTweaks t = c.tweaks (shownKind);
    t.fidelity = (float) fid.getValue();
    t.bass = (float) bass.getValue();
    t.surround = (float) surr.getValue();
    t.ambience = (float) amb.getValue();
    c.setTweaks (shownKind, t);
}

void MainComponent::timerCallback()
{
    if (! isShowing()) return;
    const auto& st = c.now();
    bool needPaint = false;

    // ease the accent toward the colour of the mode in use
    const juce::Colour tc = kindColour (st.kind);
    if (kcCur != tc)
    {
        kcCur = lerpColour (kcCur, tc, 0.16f);
        if (std::abs ((int) kcCur.getRed() - (int) tc.getRed()) + std::abs ((int) kcCur.getGreen() - (int) tc.getGreen())
            + std::abs ((int) kcCur.getBlue() - (int) tc.getBlue()) < 6) kcCur = tc;
        for (auto* sl : { &fid, &bass, &surr, &amb }) sl->setColour (juce::Slider::trackColourId, kcCur);
        toneSwitch->onColour = kcCur;
        needPaint = true;
    }
    if (! demoData) spectrum->set (st.bands, st.bandPeaks, kcCur, c.enabled() && st.running);
    if (! demoData && ++graphTick % 6 == 0)
        graph->set (st.history, targetLufsFor (c.volume()) + c.sound (st.kind).targetOffsetDb, c.leveler() && c.enabled(), kcCur);
    if (st.kind != shownKind) syncFromController();
    static juce::String lastNow;
    const juce::String nowKey = st.title + st.artist + whyText() + toneText() + (st.running ? "1" : "0");
    if (nowKey != lastNow) { lastNow = nowKey; needPaint = true; }
    if (eqOpen || eqAnim > 0.0f) eq->set (c.tweaks (shownKind).eq, c.sound (shownKind).eq, st.tone, kcCur);
    if (needPaint) repaint();
}

juce::String MainComponent::whyText() const
{
    const auto& st = c.now();
    switch (st.why)
    {
        case Why::Manual: return L ("вы выбрали", "you picked it");
        case Why::ManualBefore: return L ("вы выбирали для этого источника", "you picked it for this source");
        case Why::Learned: return L ("запомнено для этого источника", "learned for this source");
        case Why::App: return L ("по программе", "by the app");
        case Why::Title: return L ("по названию", "by the title") + (st.whyDetail.isNotEmpty() ? juce::String (L (": «", ": \"")) + st.whyDetail + L ("»", "\"") : juce::String());
        case Why::Sound: return L ("по звуку", "by the sound");
        default: return L ("пока по умолчанию", "default for now");
    }
}

juce::String MainComponent::toneText() const
{
    const auto& st = c.now();
    if (! c.toneAuto() || c.sound (st.kind).toneStrength <= 0 || st.toneConfidence < 1.0f) return {};
    // Low = 60-210 Hz, mids = 400 Hz-1.4 kHz, clarity = 2.5-8.6 kHz (16 kHz hardly matters to the ear).
    const float low = (st.tone[0] + st.tone[1] + st.tone[2]) / 3;
    const float mid = (st.tone[3] + st.tone[4] + st.tone[5]) / 3;
    const float high = (st.tone[6] + st.tone[7] + st.tone[8]) / 3;
    juce::StringArray parts;
    if (low < -0.7f) parts.add (L ("убран гул", "less boom"));
    if (low > 0.7f) parts.add (L ("добавлено низов", "more body"));
    if (high > 0.7f) parts.add (L ("добавлена чёткость", "more clarity"));
    if (high < -0.7f) parts.add (L ("смягчена резкость", "less harshness"));
    if (std::abs (mid) > 0.7f && parts.isEmpty()) parts.add (mid > 0 ? L ("ярче середина", "fuller mids") : L ("мягче середина", "softer mids"));
    if (! parts.isEmpty()) return parts.joinIntoString (" · ");
    float most = 0;
    for (float t : st.tone) most = std::max (most, std::abs (t));
    return most > 0.7f ? L ("небольшая правка тона", "a small tone touch-up") : juce::String();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    const auto& st = c.now();
    const juce::Colour kc = kcCur;
    neoTint() = kc;

    // The liquid glass: the scene behind, then the baked panes over it (rebuilt when the size or the
    // pixel density changes).
    {
        const float scale = std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 20.0f) / 20.0f;
        std::vector<juce::Rectangle<int>> panes { nowR, loudR, soundR };
        if (banner() != Banner::None) panes.push_back (bannerR);
        if (! glass.matches (getWidth(), getHeight(), scale, panes))
        {
            glass.build (getWidth(), getHeight(), scale, panes);
        }
        glassOn() = true;
        g.drawImage (glass.scene, getLocalBounds().toFloat());
        // the mood of the current mode as a soft light over the scene
        g.setGradientFill (juce::ColourGradient (kc.withAlpha (0.08f), 150.0f, -40.0f, kc.withAlpha (0.0f), 150.0f + 620.0f, 520.0f, true));
        g.fillRect (getLocalBounds());
        g.drawImage (glass.panes, getLocalBounds().toFloat());
    }

    // header: mark, name, status
    const auto lg = juce::Rectangle<float> ((float) headerR.getX(), (float) headerR.getY() + 6.0f, 52.0f, 52.0f);
    if (logo.isValid()) g.drawImage (logo, lg, juce::RectanglePlacement::centred);
    g.setColour (col::text);
    g.setFont (font (30.0f, 700));
    g.drawText (appName(), juce::Rectangle<int> (headerR.getX() + 66, headerR.getY() + 2, 220, 36), juce::Justification::centredLeft);
    const bool working = c.enabled() && st.installed && st.running;
    const juce::Colour dot = ! c.enabled() ? col::faint : (working ? col::ok : col::warn);
    const float dotY = (float) headerR.getY() + 51.0f;
    if (working)
    {
        g.setColour (dot.withAlpha (0.25f));
        g.fillEllipse ((float) headerR.getX() + 66.0f, dotY - 5.0f, 14.0f, 14.0f);
    }
    g.setColour (dot);
    g.fillEllipse ((float) headerR.getX() + 69.0f, dotY - 2.0f, 8.0f, 8.0f);
    g.setColour (col::dim);
    g.setFont (font (15.0f, 500));
    const juce::String status = ! c.enabled() ? L ("выключено", "off")
                              : ! st.installed ? L ("не подключено к выходу", "not connected to the output")
                              : ! st.running ? L ("ждёт звука", "waiting for sound")
                              : L ("работает", "working");
    g.drawText (status, juce::Rectangle<int> (headerR.getX() + 88, headerR.getY() + 40, 240, 22), juce::Justification::centredLeft, true);

    // banner
    if (banner() != Banner::None)
    {
        drawCard (g, bannerR.toFloat());
        g.setColour (col::warn.withAlpha (0.12f));
        g.fillRoundedRectangle (bannerR.toFloat().reduced (1.0f), GlassBackdrop::kRadius);
        g.setColour (col::warn);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) bannerR.getX() + 18.0f, (float) bannerR.getCentreY() - 10.0f, 4.0f, 20.0f), 2.0f);
        g.setFont (font (15.0f, 500));
        const auto text = banner() == Banner::NotInstalled
            ? L ("«Ровно» ещё не подключён к этому выходу. Windows спросит разрешение, звук пропадёт на секунду.",
                 "Evenly is not connected to this output yet. Windows will ask for permission; sound stops for a second.")
            : L ("Звук сейчас идёт через FxSound, обработка будет двойной. Переключить вывод на этот выход?",
                 "Sound goes through FxSound now, so it would be processed twice. Switch the output to this device?");
        g.drawFittedText (text, bannerR.reduced (40, 8).withTrimmedRight (150), juce::Justification::centredLeft, 2);
    }

    // now playing
    drawCard (g, nowR.toFloat());
    // The icon and the text block are both centred vertically in the card; the artist line takes room only when there is one.
    auto nr = nowR.reduced (20, 0);
    const auto badge = juce::Rectangle<float> ((float) nr.getX(), (float) nowR.getCentreY() - 38.0f, 76.0f, 76.0f);
    {
        g.setColour (col::bg.withAlpha (0.8f));
        g.fillEllipse (badge.reduced (4.0f));
        juce::Path ring;
        ring.addEllipse (badge.reduced (4.0f));
        glowStroke (g, ring, kc, 1.6f, 1.0f);
        drawKindIcon (g, st.kind, badge.reduced (20.0f), kc.brighter (0.25f), 2.0f);
    }
    nr.removeFromLeft (76 + 18);
    const bool unnamed = st.title.isEmpty() && st.app.isEmpty();
    const bool sounding = st.running && st.inLufs > -50;
    const bool nothing = unnamed && ! sounding;
    const juce::String title = ! unnamed ? (st.title.isNotEmpty() ? st.title : L ("Звук без названия", "Untitled sound"))
                             : sounding ? L ("Звук без названия", "Untitled sound")
                                        : L ("Сейчас ничего не играет", "Nothing is playing");
    juce::String sub = st.artist;
    if (st.app.isNotEmpty()) sub = sub.isNotEmpty() ? sub + "  ·  " + st.app : st.app;
    if (nothing || unnamed) sub = {};
    const int blockH = 28 + (sub.isNotEmpty() ? 26 : 0) + 12 + 30;
    nr = nr.withHeight (blockH).withY (nowR.getCentreY() - blockH / 2);
    g.setColour (col::text);
    g.setFont (font (19.0f, 600));
    g.drawFittedText (title, nr.removeFromTop (28), juce::Justification::centredLeft, 1, 0.9f);
    if (sub.isNotEmpty())
    {
        g.setColour (col::dim);
        g.setFont (font (15.0f));
        g.drawText (sub, nr.removeFromTop (26), juce::Justification::centredLeft, true);
    }
    nr.removeFromTop (12);
    auto chip = nr.removeFromTop (30);
    const juce::String kn = kindName (st.kind);
    g.setFont (font (14.0f, 600));
    const int cw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), kn) + 28;
    auto chipR = chip.removeFromLeft (cw).toFloat();
    g.setColour (kc.withAlpha (0.20f));
    g.fillRoundedRectangle (chipR, 15.0f);
    g.setColour (kc.brighter (0.25f));
    g.drawText (kn, chipR, juce::Justification::centred);
    g.setColour (col::dim);
    g.setFont (font (14.0f));
    g.drawText (whyText(), chip.withTrimmedLeft (12), juce::Justification::centredLeft, true);

    // loudness
    drawCard (g, loudR.toFloat());
    auto lr = loudR.reduced (24, 18);
    g.setColour (col::text);
    g.setFont (font (19.0f, 600));
    g.drawText (L ("Ровная громкость", "Even loudness"), lr.removeFromTop (28).withTrimmedRight (70), juce::Justification::centredLeft);
    lr.removeFromTop (24);
    // two-line legend sits at the right of the graph's top edge
    {
        const int gy = graph->getY() - 6;
        const bool on = c.leveler() && c.enabled();
        g.setFont (font (13.0f, 500));
        const auto t1 = L ("как пришло", "as it came"), t2 = L ("как звучит", "as you hear it");
        const int w2 = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t2), w1 = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t1);
        int x = loudR.getRight() - 24 - w2;
        g.setColour (col::dim);
        g.drawText (t2, x, gy - 18, w2, 18, juce::Justification::centredRight);
        g.setColour (on ? kc : col::dim);
        g.fillRoundedRectangle ((float) x - 24.0f, (float) gy - 10.0f, 16.0f, 3.0f, 1.5f);
        x -= 24 + 20 + w1;
        g.setColour (col::dim);
        g.drawText (t1, x, gy - 18, w1, 18, juce::Justification::centredRight);
        g.setColour (col::dim.withAlpha (0.6f));
        g.fillRoundedRectangle ((float) x - 24.0f, (float) gy - 9.5f, 16.0f, 2.0f, 1.0f);
    }
    auto volRow = loudR.reduced (24, 0).withTop (volume.getY()).withHeight (volume.getHeight());
    g.setColour (col::dim);
    g.setFont (font (15.0f, 500));
    g.drawText (L ("Громкость", "Volume"), volRow.removeFromLeft (140), juce::Justification::centredLeft);
    g.setColour (col::text);
    g.setFont (font (15.0f, 600));
    g.drawText (juce::String (juce::roundToInt (c.volume() * 100)) + "%", volRow.removeFromRight (52), juce::Justification::centredRight);

    // sound
    drawCard (g, soundR.toFloat());
    auto sr = soundR.reduced (24, 18);
    g.setColour (col::text);
    g.setFont (font (19.0f, 600));
    g.drawText (L ("Звук: ", "Sound: ") + kindName (shownKind), sr.removeFromTop (28).withTrimmedRight (110), juce::Justification::centredLeft);
    const juce::String names[4] = { L ("Чёткость", "Clarity"), L ("Бас", "Bass"), L ("Объём", "Width"), L ("Атмосфера", "Ambience") };
    juce::Slider* sliders[4] = { &fid, &bass, &surr, &amb };
    for (int i = 0; i < 4; ++i)
    {
        auto row = juce::Rectangle<int> (soundR.getX() + 24, sliders[i]->getY(), soundR.getWidth() - 48, sliders[i]->getHeight());
        g.setColour (col::dim);
        g.setFont (font (15.0f, 500));
        g.drawText (names[i], row.removeFromLeft (104), juce::Justification::centredLeft);
        g.setColour (col::text);
        g.setFont (font (15.0f, 600));
        g.drawText (juce::String (juce::roundToInt (sliders[i]->getValue() * 100)) + "%", row.removeFromRight (52), juce::Justification::centredRight);
    }
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (soundR.getX() + 24, toneSwitch->getY() - 14, soundR.getWidth() - 48, 1);
    g.setColour (col::text);
    g.setFont (font (15.5f, 600));
    g.drawText (L ("Подстраивать тон под запись", "Fix the tone of each recording"),
                juce::Rectangle<int> (soundR.getX() + 24, toneSwitch->getY(), soundR.getWidth() - 48 - 64, toneSwitch->getHeight()), juce::Justification::centredLeft, true);
    g.setColour (col::dim);
    g.setFont (font (14.0f));
    if (toneText().isNotEmpty())
        g.drawFittedText (toneText(), juce::Rectangle<int> (soundR.getX() + 24, toneSwitch->getBottom() + 4, soundR.getWidth() - 48, 44), juce::Justification::topLeft, 2);

}

void MainComponent::resized()
{
    auto r = getLocalBounds().withTrimmedTop (kTop).withTrimmedBottom (kBottom).reduced (kPad, 0);
    headerR = r.removeFromTop (kHead);
    {
        // right side of the header: output, more, power
        auto hr = headerR;
        power->setBounds (hr.removeFromRight (62).withSizeKeepingCentre (62, 38).expanded (Switch::kGlow - 4));
        hr.removeFromRight (10);
        moreButton.setBounds (hr.removeFromRight (84).withSizeKeepingCentre (84 - 3, 44 - 3).expanded (NeoButton::kGlow));
        hr.removeFromRight (10);
        deviceBox->setBounds (hr.removeFromRight (340).withSizeKeepingCentre (340, 44));
    }
    r.removeFromTop (kGap);
    bannerR = {};
    if (banner() != Banner::None)
    {
        bannerR = r.removeFromTop (kBannerH);
        bannerButton.setBounds (juce::Rectangle<int> (bannerR.getRight() - 150, bannerR.getCentreY() - 21, 134, 42).reduced (2).expanded (NeoButton::kGlow));
        r.removeFromTop (kGap);
    }
    auto row1 = r.removeFromTop (kRow1);
    nowR = row1.removeFromLeft (560);
    row1.removeFromLeft (kGap - 8);
    modesR = row1.withTrimmedLeft (8);
    modes->setBounds (modesR.expanded (ModeBar::kGlow));
    r.removeFromTop (kGap);

    auto row2 = r.removeFromTop (kRow2);
    loudR = row2.removeFromLeft (640);
    row2.removeFromLeft (kGap);
    soundR = row2;
    levelSwitch->setBounds (juce::Rectangle<int> (loudR.getRight() - 24 - 56, loudR.getY() + 16, 56, 34).expanded (Switch::kGlow - 4));
    spectrum->setBounds (loudR.getX() + 24, loudR.getY() + 66, loudR.getWidth() - 48, 150);
    graph->setBounds (loudR.getX() + 24, loudR.getY() + 242, loudR.getWidth() - 48, 108);
    volume.setBounds (loudR.getX() + 24 + 140, loudR.getBottom() - 18 - 36, loudR.getWidth() - 48 - 140 - 56, 36);

    resetSound.setBounds (juce::Rectangle<int> (soundR.getRight() - 24 - 104, soundR.getY() + 15, 104, 34).reduced (2).expanded (NeoButton::kGlow));
    {
        int y = soundR.getY() + 66;
        for (auto* s : { &fid, &bass, &surr, &amb })
        {
            s->setBounds (soundR.getX() + 24 + 104, y, soundR.getWidth() - 48 - 104 - 56, 40);
            y += 50;
        }
        toneSwitch->setBounds (juce::Rectangle<int> (soundR.getRight() - 24 - 56, y + 20, 56, 34).expanded (Switch::kGlow - 4));
    }
    r.removeFromTop (kGap);
    eqR = r.removeFromTop (kEqClosed);
    layoutEq();
}

void MainComponent::bannerAction()
{
    if (banner() == Banner::NotInstalled)
    {
        juce::String err;
        if (! c.installOnDevice (true, err) && err != "cancelled")
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, L ("Не получилось", "That did not work"),
                                                    L ("Не удалось подключить «Ровно» к выходу: ", "Could not connect Evenly to the output: ") + err);
    }
    else if (banner() == Banner::FxSound)
    {
        c.switchDefaultToDevice();   // no "done" box: the banner simply goes away
    }
    syncFromController();
}

void MainComponent::showMoreMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    // Show what the keep-awake is actually doing: when it cannot hold the output, the first sound
    // after a pause is swallowed and there is nothing in the window to explain why.
    const auto& ks = c.now();
    juce::String keepText = L ("Не давать выходу засыпать", "Keep the output awake");
    if (c.keepAwake())
        switch (ks.keepStatus)
        {
            case KeepAlive::Status::Holding: keepText += L ("  ·  держит", "  ·  holding"); break;
            case KeepAlive::Status::Waiting: keepText += L ("  ·  выход недоступен, жду", "  ·  output unavailable, waiting"); break;
            case KeepAlive::Status::Failed:  keepText += L ("  ·  не вышло", "  ·  failed") + juce::String (" (") + ks.keepStep
                                                       + " 0x" + juce::String::toHexString ((int) ks.keepError) + ")"; break;
            default: break;
        }
    m.addItem (500, L ("Alt + ` — быстро сменить режим из любого окна", "Alt + ` switches the mode from any window"), false);
    m.addSeparator();
    m.addItem (1, keepText, true, c.keepAwake());
    m.addItem (2, L ("Громкость как в FxSound (без выравнивания)", "FxSound-style loudness (no leveling)"), true, c.fxSoundLoudness());
    m.addSeparator();
    juce::PopupMenu scale;
    scale.setLookAndFeel (&lnf);
    scale.addItem (20, L ("Авто (по размеру монитора)", "Automatic (by monitor size)"));
    for (int p : { 100, 125, 150, 175, 200 }) scale.addItem (p, juce::String (p) + " %");
    m.addSubMenu (L ("Размер окна", "Window size"), scale);
    juce::PopupMenu langs;
    langs.setLookAndFeel (&lnf);
    langs.addItem (299, L ("Авто", "Automatic"), true, languageChoice().isEmpty());
    for (int i = 0; i < kLangCount; ++i) langs.addItem (300 + i, langName ((Lang) i), true, languageChoice() == langCode ((Lang) i));
    m.addSubMenu (L ("Язык", "Language"), langs);
    m.addSeparator();
    m.addItem (3, L ("Забыть, что программа выучила", "Forget what the app has learned"));
    m.addItem (4, L ("Отключить «Ровно» от этого выхода", "Disconnect Evenly from this output"), c.now().installed);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton).withMinimumWidth (300), [this] (int r)
    {
        if (r == 1) c.setKeepAwake (! c.keepAwake());
        else if (r == 2) c.setFxSoundLoudness (! c.fxSoundLoudness());
        else if (r == 3) c.forgetLearned();
        else if (r == 4) { juce::String err; c.installOnDevice (false, err); }
        else if (r == 20 && onScale) onScale (0);
        else if (r >= 100 && r <= 200 && onScale) onScale (r);
        else if (r == 299) { useAutomaticLanguage(); refreshTexts(); }
        else if (r >= 300 && r < 300 + kLangCount) { chooseLanguage ((Lang) (r - 300)); refreshTexts(); }
        syncFromController();
    });
}

// Texts that are set once on a component (the rest is drawn from L() on every paint).
void MainComponent::refreshTexts()
{
    resetSound.setButtonText (L ("Сбросить", "Reset"));
    moreButton.setButtonText (L ("Ещё", "More"));
    if (auto* top = getTopLevelComponent()) top->setName (appName());
    if (languageListener()) languageListener()();
    repaint();
}
} // namespace rv::ui
