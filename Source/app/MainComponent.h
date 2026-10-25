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
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.5f, 500); }
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override { return Fonts::get().regular; }
};

class Switch;
class ModeBar;
class LevelGraph;
class EqEditor;

class MainComponent : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MainComponent (Controller&);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void setEqOpen (bool open);
    int preferredHeight() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncFromController(); }
    void timerCallback() override;
    void syncFromController();
    void soundChanged();
    void showMoreMenu();
    void bannerAction();
    juce::String toneText() const;
    juce::String whyText() const;
    enum class Banner { None, NotInstalled, FxSound };
    Banner banner() const;

    Controller& c;
    RovnoLookAndFeel lnf;
    std::unique_ptr<Switch> power, levelSwitch, toneSwitch;
    std::unique_ptr<ModeBar> modes;
    std::unique_ptr<LevelGraph> graph;
    std::unique_ptr<EqEditor> eq;
    juce::Slider volume, fid, bass, surr, amb;
    juce::TextButton resetSound, bannerButton, moreButton;
    juce::ComboBox deviceBox;
    juce::StringArray deviceGuids;
    Kind shownKind = Kind::Unknown;
    bool eqOpen = false, syncing = false;
    juce::Rectangle<int> headerR, bannerR, nowR, modesR, loudR, soundR, eqR, footR, eqHeadR;
};
} // namespace rv::ui
