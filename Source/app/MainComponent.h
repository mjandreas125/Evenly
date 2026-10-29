#pragma once
// The main window's content.
#include "Controller.h"
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
    int getPopupMenuBorderSize() override { return 14; }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& idealWidth, int& idealHeight) override;
};

class Switch;
class ModeBar;
class Spectrum;
class LevelGraph;
class EqEditor;
class DeviceBox;

class MainComponent : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MainComponent (Controller&);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void setEqOpen (bool open);
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
    std::unique_ptr<DeviceBox> deviceBox;
    juce::Slider volume, fid, bass, surr, amb;
    juce::TextButton resetSound, bannerButton, moreButton;
    juce::StringArray deviceGuids;
    Kind shownKind = Kind::Unknown;
    bool eqOpen = false, syncing = false, eqHover = false;
    float eqAnim = 0.0f;                 // 0 closed .. 1 open (chevron, fade of the plot)
    juce::Colour kcCur = col::accent;    // the mode colour, eased toward the current mode's
    juce::Image logo;
    int graphTick = 0;
    juce::Rectangle<int> headerR, bannerR, nowR, modesR, loudR, soundR, eqR, footR, eqHeadR;
};
} // namespace rv::ui
