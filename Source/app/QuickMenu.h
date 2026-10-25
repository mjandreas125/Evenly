#pragma once
// The quick menu: Ctrl+Alt+` anywhere in Windows opens a small panel over everything with the
// five modes; 1-5, arrows + Enter or a click picks one, Esc or clicking elsewhere closes it.
#include "Controller.h"
#include "Theme.h"

namespace rv::ui
{
class QuickMenu : public juce::Component, private juce::Timer
{
public:
    explicit QuickMenu (Controller&);
    ~QuickMenu() override;
    void toggle();
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void focusLost (FocusChangeType) override { hide(); }

private:
    void timerCallback() override;
    void show();
    void hide();
    void choose (int index);
    int tileAt (juce::Point<int>) const;
    juce::Rectangle<float> tile (int i) const;

    Controller& c;
    int focus = 0;
    float alpha = 0, target = 0;
    struct Hotkey;
    std::unique_ptr<Hotkey> hotkey;
};
} // namespace rv::ui
