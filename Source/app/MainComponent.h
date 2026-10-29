#pragma once
// The main window's content.
#include "Controller.h"
#include "Glass.h"
#include "Theme.h"

namespace rv::ui
{
class RovnoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RovnoLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getPopupMenuFont() override { return font (15.0f, 500); }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override { return Fonts::get().regular; }

    // message boxes: small round badge instead of JUCE's giant icon, larger text
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    juce::Font getAlertWindowTitleFont() override { return font (20.0f, 600); }
    juce::Font getAlertWindowMessageFont() override { return font (16.0f); }
    juce::Font getAlertWindowFont() override { return font (15.0f, 500); }
    int getAlertWindowButtonHeight() override { return 44; }

    // popup menus: rounded floating panel with its own soft shadow, pill highlight, tick mark
    int getMenuWindowFlags() override { return juce::ComponentPeer::windowIsSemiTransparent; }
    int getPopupMenuBorderSize() override { return 8; }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& idealWidth, int& idealHeight) override;
};

// A button that eases into hover and press instead of jumping.
class NeoButton : public juce::TextButton, private FrameTimer
{
public:
    static constexpr int kGlow = 12;   // place the button with bounds().expanded (kGlow)
    NeoButton() : FrameTimer (this) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics& g, bool, bool) override
    {
        const bool primary = getProperties().getWithDefault ("primary", false);
        const juce::Colour c = primary ? col::accent : neoTint();
        auto r = getLocalBounds().toFloat().reduced (kGlow + pressA * 1.2f);   // the rest of the bounds is for the glow
        juce::Path p;
        p.addRoundedRectangle (r, 10.0f);
        if (hoverA > 0.02f) softGlow (g, p, c, (primary ? 0.45f : 0.30f) * hoverA, 10.0f);
        g.setGradientFill (juce::ColourGradient (c.withAlpha ((primary ? 0.30f : 0.06f) + hoverA * 0.10f + pressA * 0.12f), 0, r.getY(),
                                                 c.withAlpha ((primary ? 0.12f : 0.02f) + hoverA * 0.03f), 0, r.getBottom(), false));
        g.fillPath (p);
        g.setColour ((primary ? c.withAlpha (0.95f) : juce::Colours::white.withAlpha (0.18f)).interpolatedWith (c.withAlpha (0.9f), primary ? 0.0f : hoverA));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        g.setColour (primary ? col::accent.brighter (0.5f) : col::text);
        g.setFont (font (15.0f, 600));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, true);
    }
    void buttonStateChanged() override { startTimerHz (60); }
private:
    void timerCallback() override
    {
        const float th = isOver() ? 1.0f : 0.0f, tp = isDown() ? 1.0f : 0.0f;
        hoverA += (th - hoverA) * rate (dt(), 18.0f);
        pressA += (tp - pressA) * rate (dt(), 32.0f);
        if (std::abs (th - hoverA) < 0.01f && std::abs (tp - pressA) < 0.01f) { hoverA = th; pressA = tp; stopTimer(); }
        repaint();
    }
    float hoverA = 0, pressA = 0;
};

// A slider whose thumb and glow ease with the mouse.
class NeoSlider : public juce::Slider, private FrameTimer
{
public:
    NeoSlider() : FrameTimer (this) {}
    float hoverAmount() const { return hoverA; }
    float pressAmount() const { return pressA; }
    void mouseEnter (const juce::MouseEvent& e) override { Slider::mouseEnter (e); startTimerHz (60); }
    void mouseExit (const juce::MouseEvent& e) override { Slider::mouseExit (e); startTimerHz (60); }
    void mouseDown (const juce::MouseEvent& e) override { Slider::mouseDown (e); startTimerHz (60); }
    void mouseUp (const juce::MouseEvent& e) override { Slider::mouseUp (e); startTimerHz (60); }
private:
    void timerCallback() override
    {
        const float th = isMouseOverOrDragging() ? 1.0f : 0.0f, tp = isMouseButtonDown() ? 1.0f : 0.0f;
        hoverA += (th - hoverA) * rate (dt(), 18.0f);
        pressA += (tp - pressA) * rate (dt(), 32.0f);
        if (std::abs (th - hoverA) < 0.01f && std::abs (tp - pressA) < 0.01f) { hoverA = th; pressA = tp; stopTimer(); }
        repaint();
    }
    float hoverA = 0, pressA = 0;
};

class Switch;
class ModeBar;
class Spectrum;
class LevelGraph;
class EqEditor;
class DeviceBox;
class EqPanel;
class Scrim;

class MainComponent : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MainComponent (Controller&);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setEqOpen (bool open, bool animate = false);   // animated from the UI, at once for snapshots
    int preferredHeight() const;
    static constexpr int kWidth = 1120;
    static inline bool hideBanner = false;   // for the README picture (--snapshot --clean)

    // Called by the window when the interface scale changes (the menu entry lives here).
    std::function<void (int percent)> onScale;   // 0 = automatic

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncFromController(); }
    void timerCallback() override;
    void syncFromController();
    void soundChanged();
    void showMoreMenu();
    void refreshTexts();
    void eqTick();
    void layoutEq();
    int eqHeight() const;
    struct Ticker : FrameTimer
    {
        explicit Ticker (juce::Component* c) : FrameTimer (c) {}
        std::function<void()> tick;
        float frameDt() const { return dt(); }
        void timerCallback() override { if (tick) tick(); }
    } ticker { this };
    void bannerAction();
    juce::String toneText() const;
    juce::String whyText() const;
    enum class Banner { None, NotInstalled, FxSound };
    Banner banner() const;

    Controller& c;
    RovnoLookAndFeel lnf;
    std::unique_ptr<Switch> power, levelSwitch, toneSwitch;
    std::unique_ptr<ModeBar> modes;
    std::unique_ptr<Spectrum> spectrum;
    std::unique_ptr<LevelGraph> graph;
    std::unique_ptr<EqEditor> eq;
    std::unique_ptr<EqPanel> eqPanel;
    std::unique_ptr<Scrim> scrim;
    std::unique_ptr<DeviceBox> deviceBox;
    NeoSlider volume, fid, bass, surr, amb;
    NeoButton resetSound, bannerButton, moreButton;
    juce::StringArray deviceGuids;
    Kind shownKind = Kind::Unknown;
    bool eqOpen = false, syncing = false, eqHover = false;
    float eqAnim = 0.0f, eqHoverAmt = 0.0f;                 // 0 closed .. 1 open (chevron, fade of the plot)
    juce::Colour kcCur = col::accent;    // the mode colour, eased toward the current mode's
    juce::Image logo;
    GlassBackdrop glass;
    bool glassDirty = true;
    int graphTick = 0;
    juce::Rectangle<int> headerR, bannerR, nowR, modesR, loudR, soundR, eqR, footR, eqHeadR;
};
} // namespace rv::ui
