#include "MainComponent.h"
#include <windows.h>

namespace rv::ui
{
bool russian()
{
    static const bool r = []
    {
        auto slav = [] (LANGID id) { const WORD p = PRIMARYLANGID (id); return p == LANG_RUSSIAN || p == LANG_UKRAINIAN || p == LANG_BELARUSIAN || p == LANG_KAZAK; };
        return slav (GetUserDefaultUILanguage()) || slav (LANGIDFROMLCID (GetUserDefaultLCID()));
    }();
    return r;
}

// ---- look and feel ---------------------------------------------------------------------------
RovnoLookAndFeel::RovnoLookAndFeel()
{
    setDefaultSansSerifTypeface (Fonts::get().regular);
    setColour (juce::PopupMenu::backgroundColourId, col::cardHi);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::line);
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::cardHi);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::line);
}

void RovnoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const juce::Rectangle<float> track ((float) x, cy - 2.5f, (float) w, 5.0f);
    g.setColour (col::line);
    g.fillRoundedRectangle (track, 2.5f);
    const juce::Colour c = s.findColour (juce::Slider::trackColourId);
    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.75f), (float) x, cy, c, pos, cy, false));
    g.fillRoundedRectangle (track.withRight (pos), 2.5f);
    const float r = s.isMouseOverOrDragging() ? 9.0f : 8.0f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (pos - r, cy - r + 1.5f, r * 2, r * 2);
    g.setColour (col::text);
    g.fillEllipse (pos - r, cy - r, r * 2, r * 2);
}

void RovnoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    if (primary)
    {
        g.setGradientFill (juce::ColourGradient (col::accent, r.getX(), r.getY(), col::accent2, r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle (r, r.getHeight() / 2);
        if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.18f : 0.10f)); g.fillRoundedRectangle (r, r.getHeight() / 2); }
    }
    else
    {
        g.setColour (down ? col::line : (over ? col::cardHi.brighter (0.06f) : col::cardHi));
        g.fillRoundedRectangle (r, r.getHeight() / 2);
        g.setColour (col::line);
        g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
    }
}

void RovnoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    g.setColour (primary ? juce::Colour (0xff1a1206) : col::text);
    g.setFont (font (13.5f, 600));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
}

void RovnoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const juce::Rectangle<float> r (0.5f, 0.5f, (float) w - 1, (float) h - 1);
    g.setColour (col::cardHi);
    g.fillRoundedRectangle (r, r.getHeight() / 2);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2);
    arrow.lineTo (ax, ay + 2);
    arrow.lineTo (ax + 4, ay - 2);
    g.setColour (col::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void RovnoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::cardHi);
    g.setColour (col::line);
    g.drawRect (0, 0, w, h);
}

// ---- switch ----------------------------------------------------------------------------------
class Switch : public juce::ToggleButton, private juce::Timer
{
public:
    Switch() { setClickingTogglesState (true); onStateChange = [this] { startTimerHz (60); }; }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const juce::Colour on = getProperties().getWithDefault ("colour", (int) col::accent.getARGB()).toString().isEmpty()
                                    ? col::accent : juce::Colour ((juce::uint32) (juce::int64) getProperties().getWithDefault ("colour", (juce::int64) col::accent.getARGB()));
        g.setColour (col::line.interpolatedWith (on, pos));
        g.fillRoundedRectangle (r, r.getHeight() / 2);
        if (over) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (r, r.getHeight() / 2); }
        const float d = r.getHeight() - 6.0f;
        const float kx = r.getX() + 3.0f + pos * (r.getWidth() - d - 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (kx, r.getY() + 4.0f, d, d);
        g.setColour (juce::Colours::white);
        g.fillEllipse (kx, r.getY() + 3.0f, d, d);
    }
private:
    void timerCallback() override
    {
        const float target = getToggleState() ? 1.0f : 0.0f;
        pos += (target - pos) * 0.35f;
        if (std::abs (target - pos) < 0.01f) { pos = target; stopTimer(); }
        repaint();
    }
    float pos = 0;
public:
    void snap() { pos = getToggleState() ? 1.0f : 0.0f; repaint(); }
};

// ---- the five modes --------------------------------------------------------------------------
class ModeBar : public juce::Component
{
public:
    std::function<void (Kind)> onPick;   // Kind::Unknown = auto
    void set (bool autoOn, Kind current) { isAuto = autoOn; now = current; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto items = order();
        const float w = (float) getWidth() / (float) items.size();
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Kind k = items[i];
            auto r = juce::Rectangle<float> (w * (float) i, 0, w, (float) getHeight()).reduced (3.0f, 0.0f);
            const bool selected = k == Kind::Unknown ? isAuto : (! isAuto && now == k);
            const bool active = k != Kind::Unknown && now == k;   // what is playing in auto mode
            const juce::Colour kc = kindColour (k);
            if (selected)
            {
                g.setColour (kc.withAlpha (0.16f));
                g.fillRoundedRectangle (r, 14.0f);
                g.setColour (kc.withAlpha (0.85f));
                g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);
            }
            else
            {
                g.setColour (hover == (int) i ? col::cardHi.brighter (0.05f) : col::cardHi);
                g.fillRoundedRectangle (r, 14.0f);
                g.setColour (isAuto && active ? kc.withAlpha (0.6f) : col::line);
                g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, isAuto && active ? 1.3f : 1.0f);
            }
            const juce::Colour ic = selected || (isAuto && active) ? kc : col::dim;
            drawKindIcon (g, k, juce::Rectangle<float> (r.getCentreX() - 12.0f, r.getY() + 12.0f, 24.0f, 24.0f), ic, 1.8f);
            g.setColour (selected ? col::text : col::dim);
            g.setFont (font (12.5f, selected ? 600 : 500));
            g.drawText (kindName (k), r.withTop (r.getY() + 41.0f).withHeight (18.0f), juce::Justification::centred);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = indexAt (e.x); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.x);
        if (i >= 0 && onPick) onPick (order()[(size_t) i]);
    }
private:
    static std::vector<Kind> order() { return { Kind::Unknown, Kind::Music, Kind::Speech, Kind::Movie, Kind::Game }; }
    int indexAt (int x) const { return juce::jlimit (0, 4, x * 5 / juce::jmax (1, getWidth())); }
    bool isAuto = true;
    Kind now = Kind::Music;
    int hover = -1;
};

// ---- loudness in and out over the last 90 s ----------------------------------------------------
class LevelGraph : public juce::Component
{
public:
    void set (const std::deque<std::pair<float, float>>& h, float target, bool on)
    {
        hist.assign (h.begin(), h.end());
        tgt = target;
        enabled = on;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float lo = -42.0f, hi = -4.0f;
        auto yOf = [&] (float v) { return r.getBottom() - (juce::jlimit (lo, hi, v) - lo) / (hi - lo) * r.getHeight(); };
        // target line
        g.setColour (col::faint.withAlpha (0.8f));
        const float ty = yOf (tgt);
        for (float x = r.getX(); x < r.getRight(); x += 8.0f) g.drawLine (x, ty, juce::jmin (x + 4.0f, r.getRight()), ty, 1.0f);
        if (hist.size() < 2) return;
        const size_t n = 180;
        const float dx = r.getWidth() / (float) (n - 1);
        auto path = [&] (bool out)
        {
            juce::Path p;
            const size_t start = n - hist.size();
            for (size_t i = 0; i < hist.size(); ++i)
            {
                const float v = out ? hist[i].second : hist[i].first;
                const float x = r.getX() + dx * (float) (start + i), y = yOf (v < -69 ? lo : v);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        g.setColour (col::dim.withAlpha (0.55f));
        g.strokePath (path (false), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
        auto outPath = path (true);
        const juce::Colour oc = enabled ? col::accent : col::dim;
        juce::Path fill (outPath);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX() + dx * (float) (n - hist.size()), r.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (oc.withAlpha (0.22f), 0, r.getY(), oc.withAlpha (0.0f), 0, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (oc);
        g.strokePath (outPath, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    std::vector<std::pair<float, float>> hist;
    float tgt = -11;
    bool enabled = true;
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
        g.setColour (col::line);
        g.drawLine (r.getX(), yOf (0), r.getRight(), yOf (0), 1.0f);
        g.setFont (font (11.0f));
        const char* labels[10] = { "60", "120", "210", "400", "730", "1.4k", "2.5k", "4.7k", "8.6k", "16k" };
        for (int i = 0; i < 10; ++i)
        {
            g.setColour (col::faint);
            g.drawText (labels[i], juce::Rectangle<float> (xOf (i) - 20, r.getBottom() + 4, 40, 14), juce::Justification::centred);
        }
        juce::Path dashed;
        for (int i = 0; i < 10; ++i)
            if (i == 0) dashed.startNewSubPath (xOf (i), yOf (base[i] + t[i])); else dashed.lineTo (xOf (i), yOf (base[i] + t[i]));
        juce::Path dash;
        const float lengths[2] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (dash, dashed, lengths, 2);
        g.setColour (col::dim.withAlpha (0.7f));
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
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int i = 0; i < 10; ++i)
        {
            const float rad = i == drag ? 6.5f : 5.0f;
            g.setColour (col::card);
            g.fillEllipse (xOf (i) - rad, yOf (base[i]) - rad, rad * 2, rad * 2);
            g.setColour (std::abs (u[i]) > 0.05f ? colour : col::dim);
            g.drawEllipse (xOf (i) - rad, yOf (base[i]) - rad, rad * 2, rad * 2, 2.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1;
        float best = 18.0f;
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
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (14.0f, 6.0f).withTrimmedBottom (16.0f); }
    float xOf (int i) const { const auto r = plot(); return r.getX() + r.getWidth() * (float) i / 9.0f; }
    float yOf (float db) const { const auto r = plot(); return r.getCentreY() - juce::jlimit (-12.0f, 12.0f, db) / 12.0f * r.getHeight() * 0.5f; }
    float valOf (float y) const { const auto r = plot(); return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * 12.0f; }
    float u[10] {}, base[10] {}, t[10] {};
    juce::Colour colour = col::accent;
    int drag = -1;
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

MainComponent::MainComponent (Controller& controller) : c (controller)
{
    setLookAndFeel (&lnf);
    power = std::make_unique<Switch>();
    levelSwitch = std::make_unique<Switch>();
    toneSwitch = std::make_unique<Switch>();
    modes = std::make_unique<ModeBar>();
    graph = std::make_unique<LevelGraph>();
    eq = std::make_unique<EqEditor>();
    for (juce::Component* comp : std::initializer_list<juce::Component*> { power.get(), levelSwitch.get(), toneSwitch.get(), modes.get(), graph.get(),
                                                                            &volume, &fid, &bass, &surr, &amb, &resetSound, &bannerButton, &moreButton, &deviceBox })
        addAndMakeVisible (comp);
    addChildComponent (eq.get());

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
    deviceBox.onChange = [this]
    {
        const int i = deviceBox.getSelectedItemIndex();
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
    setSize (452, preferredHeight());
    syncFromController();
    startTimerHz (12);
}

MainComponent::~MainComponent()
{
    c.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

MainComponent::Banner MainComponent::banner() const
{
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
    for (auto* sl : { &fid, &bass, &surr, &amb }) sl->setColour (juce::Slider::trackColourId, kc);
    for (Switch* sw : { power.get(), levelSwitch.get(), toneSwitch.get() }) sw->snap();
    const auto tw = c.tweaks (shownKind);
    eq->set (tw.eq, s.eq, st.tone, kc);

    deviceBox.clear (juce::dontSendNotification);
    deviceGuids.clear();
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
        deviceBox.addItem (label, deviceGuids.size());
        if (juce::String (d.guid.c_str()) == c.deviceGuid()) sel = deviceGuids.size() - 1;
    }
    if (sel >= 0) deviceBox.setSelectedItemIndex (sel, juce::dontSendNotification);

    const auto b = banner();
    bannerButton.setVisible (b != Banner::None);
    bannerButton.setButtonText (b == Banner::NotInstalled ? L ("Подключить", "Connect") : L ("Переключить", "Switch"));
    if (getHeight() != preferredHeight()) setSize (452, preferredHeight()); else resized();
    repaint();
}

int MainComponent::preferredHeight() const
{
    return 16 + 54 + 10 + (banner() != Banner::None ? 88 : 0) + 124 + 12 + 68 + 12 + 186 + 12 + 230 + 12 + (eqOpen ? 196 : 48) + 12 + 60 + 16;
}

void MainComponent::setEqOpen (bool open)
{
    eqOpen = open;
    setSize (452, preferredHeight());
    resized();
    repaint();
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
    const auto& st = c.now();
    graph->set (st.history, targetLufsFor (c.volume()) + c.sound (st.kind).targetOffsetDb, c.leveler() && c.enabled());
    if (st.kind != shownKind) syncFromController();
    static juce::String lastNow;
    const juce::String nowKey = st.title + st.artist + whyText() + toneText() + (st.running ? "1" : "0");
    if (nowKey != lastNow) { lastNow = nowKey; repaint(); }
    if (eqOpen) eq->set (c.tweaks (shownKind).eq, c.sound (shownKind).eq, st.tone, kindColour (shownKind));
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
    if (! c.toneAuto()) return L ("выключено", "off");
    if (c.sound (st.kind).toneStrength <= 0) return L ("в этом режиме не нужна", "not used in this mode");
    if (st.toneConfidence < 1.0f) return L ("слушаю запись…", "listening…");
    float low = 0, mid = 0, high = 0;
    for (int i = 0; i < 3; ++i) low += st.tone[(size_t) i];
    for (int i = 3; i < 7; ++i) mid += st.tone[(size_t) i];
    for (int i = 7; i < 10; ++i) high += st.tone[(size_t) i];
    low /= 3; mid /= 4; high /= 3;
    juce::StringArray parts;
    if (low < -0.7f) parts.add (L ("убран гул", "less boom"));
    if (low > 0.7f) parts.add (L ("добавлено низов", "more body"));
    if (high > 0.7f) parts.add (L ("добавлена чёткость", "more clarity"));
    if (high < -0.7f) parts.add (L ("смягчена резкость", "less harshness"));
    if (std::abs (mid) > 0.7f && parts.isEmpty()) parts.add (mid > 0 ? L ("ярче середина", "fuller mids") : L ("мягче середина", "softer mids"));
    return parts.isEmpty() ? L ("запись звучит нормально, правка не нужна", "the recording sounds fine as it is") : parts.joinIntoString (" · ");
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    const auto& st = c.now();
    const juce::Colour kc = kindColour (st.kind);

    // header
    g.setColour (col::text);
    g.setFont (font (27.0f, 700));
    g.drawText (L ("Ровно", "Rovno"), headerR.withHeight (34), juce::Justification::centredLeft);
    const bool working = c.enabled() && st.installed && st.running;
    const juce::Colour dot = ! c.enabled() ? col::faint : (working ? col::ok : col::warn);
    g.setColour (dot);
    g.fillEllipse ((float) headerR.getX() + 1.0f, (float) headerR.getY() + 41.0f, 7.0f, 7.0f);
    g.setColour (col::dim);
    g.setFont (font (13.0f, 500));
    const juce::String status = ! c.enabled() ? L ("выключено", "off")
                              : ! st.installed ? L ("не подключено к выходу", "not connected to the output")
                              : ! st.running ? L ("ждёт звука", "waiting for sound")
                              : L ("работает", "working");
    g.drawText (status + "  ·  " + st.deviceName, headerR.withTrimmedTop (34).withTrimmedLeft (14).withTrimmedRight (70), juce::Justification::centredLeft, true);

    // banner
    if (banner() != Banner::None)
    {
        drawCard (g, bannerR.toFloat(), col::warn.withAlpha (0.10f).overlaidWith (col::card.withAlpha (0.85f)));
        g.setColour (col::warn);
        g.setFont (font (13.5f, 600));
        const auto text = banner() == Banner::NotInstalled
            ? L ("«Ровно» ещё не подключён к этому выходу. Windows спросит разрешение, звук пропадёт на секунду.",
                 "Rovno is not connected to this output yet. Windows will ask for permission; sound stops for a second.")
            : L ("Звук сейчас идёт через FxSound, обработка будет двойной. Переключить вывод на этот выход?",
                 "Sound goes through FxSound now, so it would be processed twice. Switch the output to this device?");
        g.drawFittedText (text, bannerR.reduced (16, 10).withTrimmedRight (122), juce::Justification::centredLeft, 3);
    }

    // now playing
    drawCard (g, nowR.toFloat());
    auto nr = nowR.reduced (16, 14);
    auto badge = nr.removeFromLeft (52).withHeight (52).toFloat();
    g.setColour (kc.withAlpha (0.16f));
    g.fillRoundedRectangle (badge, 14.0f);
    drawKindIcon (g, st.kind, badge.reduced (13.0f), kc, 2.0f);
    nr.removeFromLeft (14);
    g.setColour (col::text);
    g.setFont (font (15.5f, 600));
    const bool nothing = st.title.isEmpty() && st.app.isEmpty();
    const juce::String title = ! nothing ? (st.title.isNotEmpty() ? st.title : L ("Звук без названия", "Untitled sound"))
                                         : L ("Сейчас ничего не играет", "Nothing is playing");
    g.drawFittedText (title, nr.removeFromTop (40), juce::Justification::topLeft, 2, 0.95f);
    g.setColour (col::dim);
    g.setFont (font (13.0f));
    juce::String sub = st.artist;
    if (st.app.isNotEmpty()) sub = sub.isNotEmpty() ? sub + "  ·  " + st.app : st.app;
    if (nothing) sub = L ("Включите видео или музыку: режим подберётся сам", "Play a video or music: the mode picks itself");
    g.drawText (sub, nr.removeFromTop (18), juce::Justification::centredLeft, true);
    nr.removeFromTop (8);
    auto chip = nr.removeFromTop (24);
    const juce::String kn = kindName (st.kind);
    g.setFont (font (12.0f, 600));
    const int cw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), kn) + 20;
    auto chipR = chip.removeFromLeft (cw).toFloat();
    g.setColour (kc.withAlpha (0.18f));
    g.fillRoundedRectangle (chipR, 12.0f);
    g.setColour (kc);
    g.drawText (kn, chipR, juce::Justification::centred);
    g.setColour (col::dim);
    g.setFont (font (12.5f));
    g.drawText (whyText(), chip.withTrimmedLeft (10), juce::Justification::centredLeft, true);

    // loudness
    drawCard (g, loudR.toFloat());
    auto lr = loudR.reduced (18, 14);
    g.setColour (col::text);
    g.setFont (font (15.0f, 600));
    g.drawText (L ("Ровная громкость", "Even loudness"), lr.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (12.5f));
    g.drawText (st.running ? L ("тонкая линия — как пришло, яркая — как звучит", "thin line: as it came, bright: as you hear it")
                           : L ("график оживёт, когда пойдёт звук", "the graph comes alive with sound"),
                lr.removeFromTop (18), juce::Justification::centredLeft, true);
    auto volRow = loudR.reduced (18, 14).removeFromBottom (24);
    g.setColour (col::dim);
    g.setFont (font (13.0f, 500));
    g.drawText (L ("Громкость", "Volume"), volRow.removeFromLeft (84), juce::Justification::centredLeft);
    g.drawText (juce::String (juce::roundToInt (c.volume() * 100)) + "%", volRow.removeFromRight (40), juce::Justification::centredRight);

    // sound
    drawCard (g, soundR.toFloat());
    auto sr = soundR.reduced (18, 14);
    g.setColour (col::text);
    g.setFont (font (15.0f, 600));
    g.drawText (L ("Звук: ", "Sound: ") + kindName (shownKind), sr.removeFromTop (24), juce::Justification::centredLeft);
    sr.removeFromTop (8);
    const char* namesRu[4] = { "Чёткость", "Бас", "Объём", "Атмосфера" };
    const char* namesEn[4] = { "Clarity", "Bass", "Width", "Ambience" };
    juce::Slider* sliders[4] = { &fid, &bass, &surr, &amb };
    for (int i = 0; i < 4; ++i)
    {
        auto row = sr.removeFromTop (30);
        g.setColour (col::dim);
        g.setFont (font (13.0f, 500));
        g.drawText (L (namesRu[i], namesEn[i]), row.removeFromLeft (92), juce::Justification::centredLeft);
        g.drawText (juce::String (juce::roundToInt (sliders[i]->getValue() * 100)) + "%", row.removeFromRight (40), juce::Justification::centredRight);
    }
    sr.removeFromTop (8);
    auto toneRow = sr.removeFromTop (22);
    g.setColour (col::text);
    g.setFont (font (13.5f, 500));
    g.drawText (L ("Подстраивать тон под запись", "Fix the tone of each recording"), toneRow.withTrimmedRight (56), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (12.5f));
    g.drawText (toneText(), sr.removeFromTop (20), juce::Justification::centredLeft, true);

    // EQ
    drawCard (g, eqR.toFloat());
    g.setColour (col::text);
    g.setFont (font (15.0f, 600));
    g.drawText (L ("Эквалайзер", "Equalizer"), eqHeadR.reduced (18, 0), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (12.5f));
    g.drawText (eqOpen ? L ("свернуть", "hide") : L ("показать", "show"), eqHeadR.reduced (18, 0), juce::Justification::centredRight);

    // footer
    g.setColour (col::faint);
    g.setFont (font (12.0f));
    g.drawText (L ("Ctrl + Alt + ` — быстро сменить режим", "Ctrl + Alt + ` switches the mode"), footR.withTrimmedTop (36), juce::Justification::centred);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (18, 16);
    headerR = r.removeFromTop (54);
    power->setBounds (headerR.getRight() - 56, headerR.getY() + 6, 52, 30);
    r.removeFromTop (10);
    bannerR = {};
    if (banner() != Banner::None)
    {
        bannerR = r.removeFromTop (78);
        bannerButton.setBounds (bannerR.getRight() - 124, bannerR.getCentreY() - 17, 110, 34);
        r.removeFromTop (10);
    }
    nowR = r.removeFromTop (124);
    r.removeFromTop (12);
    modesR = r.removeFromTop (68);
    modes->setBounds (modesR);
    r.removeFromTop (12);
    loudR = r.removeFromTop (186);
    levelSwitch->setBounds (loudR.getRight() - 18 - 46, loudR.getY() + 15, 46, 26);
    graph->setBounds (loudR.reduced (18, 0).withTop (loudR.getY() + 62).withHeight (78));
    volume.setBounds (loudR.reduced (18, 14).removeFromBottom (24).withTrimmedLeft (90).withTrimmedRight (48));
    r.removeFromTop (12);
    soundR = r.removeFromTop (230);
    resetSound.setBounds (soundR.getRight() - 18 - 92, soundR.getY() + 12, 92, 28);
    {
        auto sr = soundR.reduced (18, 14).withTrimmedTop (32);
        for (auto* s : { &fid, &bass, &surr, &amb })
            s->setBounds (sr.removeFromTop (30).withTrimmedLeft (98).withTrimmedRight (48));
        sr.removeFromTop (8);
        toneSwitch->setBounds (sr.getRight() - 46, sr.getY() - 2, 46, 26);
    }
    r.removeFromTop (12);
    eqR = r.removeFromTop (eqOpen ? 196 : 48);
    eqHeadR = eqR.withHeight (48);
    eq->setVisible (eqOpen);
    if (eqOpen) eq->setBounds (eqR.withTrimmedTop (44).reduced (8, 8));
    r.removeFromTop (12);
    footR = r.removeFromTop (60);
    auto fr = footR.withHeight (34);
    moreButton.setBounds (fr.removeFromRight (78));
    fr.removeFromRight (10);
    deviceBox.setBounds (fr);
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    if (eqHeadR.contains (e.getPosition())) setEqOpen (! eqOpen);
}

void MainComponent::bannerAction()
{
    if (banner() == Banner::NotInstalled)
    {
        juce::String err;
        if (! c.installOnDevice (true, err) && err != "cancelled")
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, L ("Не получилось", "That did not work"),
                                                    L ("Не удалось подключить «Ровно» к выходу: ", "Could not connect Rovno to the output: ") + err);
    }
    else if (banner() == Banner::FxSound)
    {
        c.switchDefaultToDevice();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, L ("Готово", "Done"),
            L ("Звук теперь идёт прямо на этот выход через «Ровно». FxSound можно закрыть: в трее, правой кнопкой по значку → Exit.",
               "Sound now goes straight to this output through Rovno. You can close FxSound: tray icon, right click, Exit."));
    }
    syncFromController();
}

void MainComponent::showMoreMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addItem (1, L ("Не давать выходу засыпать (HDMI, DisplayPort)", "Keep the output awake (HDMI, DisplayPort)"), true, c.keepAwake());
    m.addItem (2, L ("Громкость как в FxSound (без выравнивания)", "FxSound-style loudness (no leveling)"), true, c.fxSoundLoudness());
    m.addSeparator();
    m.addItem (3, L ("Забыть, что программа выучила", "Forget what the app has learned"));
    m.addItem (4, L ("Отключить «Ровно» от этого выхода", "Disconnect Rovno from this output"), c.now().installed);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton), [this] (int r)
    {
        if (r == 1) c.setKeepAwake (! c.keepAwake());
        else if (r == 2) c.setFxSoundLoudness (! c.fxSoundLoudness());
        else if (r == 3) c.forgetLearned();
        else if (r == 4) { juce::String err; c.installOnDevice (false, err); }
        syncFromController();
    });
}
} // namespace rv::ui
