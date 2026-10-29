#pragma once
// The quick menu. Alt + ` (the key left of 1, on any keyboard layout) opens a small panel over
// everything with the five modes, with the NEXT mode already lit:
//   - do nothing: after about a second that mode is applied and the panel folds away;
//   - tap the key again (or hold Alt and tap): the light moves on, release Alt to apply;
//   - or use the mouse: hover lights a tile, a click applies it; Esc closes without changing anything.
// The window never takes the keyboard focus, so whatever you were typing into keeps it.
#include "Controller.h"
#include "Theme.h"

namespace rv::ui
{
class QuickMenu : public juce::Component, private juce::Timer
{
public:
    explicit QuickMenu (Controller&);
    ~QuickMenu() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { mouseInside = true; }
    void mouseExit (const juce::MouseEvent&) override { mouseInside = false; outSince = nowMs(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void keyTapped();            // Alt + ` pressed
    void altReleased();
    void escapePressed();
    void show();
    void hide();
    void choose (int index);
    int tileAt (juce::Point<int>) const;
    juce::Rectangle<float> tile (int i) const;
    int currentIndex() const;
    static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    Controller& c;
    int focus = 0;
    bool shown = false, keyboardUsed = false, autoApply = false, mouseInside = false;
    float anim = 0.0f;               // 0 hidden .. 1 shown (eased when painted)
    float flash = 0.0f;              // the chosen tile blinks once
    double lastTap = 0, outSince = 0, lastFrame = 0;
    float dt() const { return frameDt; }
    float frameDt = 1.0f / 60.0f;
    juce::Point<int> lastMouse;
    juce::Image shadowCache;         // the soft shadow under the panel, blurred once
    std::array<float, 5> glow {};    // eased highlight per tile
    struct Hotkey;
    std::unique_ptr<Hotkey> hotkey;
};
} // namespace rv::ui
