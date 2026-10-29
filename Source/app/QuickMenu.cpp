#include "QuickMenu.h"
#include <windows.h>

namespace rv::ui
{
namespace
{
const Kind kTiles[5] = { Kind::Unknown, Kind::Music, Kind::Speech, Kind::Movie, Kind::Game };
constexpr int kMargin = 24;             // room for the glow around the panel
} // namespace

// A low-level keyboard hook, so that the shortcut works on every layout (it looks at the physical
// key, scan code 0x29) and without registering a hotkey that other programs could not use. Nothing
// is swallowed except Esc while the panel is open; Alt + ` still reaches the program in front.
struct QuickMenu::Hotkey
{
    Hotkey (std::function<void()> tap, std::function<void()> altUp, std::function<bool()> esc)
        : onTap (std::move (tap)), onAltUp (std::move (altUp)), onEsc (std::move (esc))
    {
        self = this;
        hook = SetWindowsHookExW (WH_KEYBOARD_LL, proc, GetModuleHandleW (nullptr), 0);
    }
    ~Hotkey()
    {
        if (hook) UnhookWindowsHookEx (hook);
        self = nullptr;
    }
    static LRESULT CALLBACK proc (int code, WPARAM w, LPARAM l)
    {
        if (code == HC_ACTION && self != nullptr)
        {
            const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*> (l);
            const bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN;
            const bool up = w == WM_KEYUP || w == WM_SYSKEYUP;
            if (k->scanCode == 0x29)
            {
                if (down)
                {
                    const bool alt = (k->flags & LLKHF_ALTDOWN) != 0, ctrl = (GetAsyncKeyState (VK_CONTROL) & 0x8000) != 0;   // AltGr counts as Ctrl
                    if (alt && ! ctrl && ! self->keyHeld) { self->keyHeld = true; juce::MessageManager::callAsync ([] { if (auto* h = self) h->onTap(); }); }   // never do work inside the hook: Windows drops a hook that is slow
                }
                else if (up) self->keyHeld = false;
            }
            else if ((k->vkCode == VK_LMENU || k->vkCode == VK_RMENU) && up) juce::MessageManager::callAsync ([] { if (auto* h = self) h->onAltUp(); });
            else if (k->vkCode == VK_ESCAPE && down && self->onEsc()) return 1;   // closes the panel; swallowed
        }
        return CallNextHookEx (nullptr, code, w, l);
    }
    std::function<void()> onTap, onAltUp;
    std::function<bool()> onEsc;
    HHOOK hook = nullptr;
    bool keyHeld = false;
    static inline Hotkey* self = nullptr;
};

QuickMenu::QuickMenu (Controller& controller) : c (controller)
{
    setSize (612 + kMargin * 2, 202 + kMargin * 2);
    setWantsKeyboardFocus (false);
    setAlwaysOnTop (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    hotkey = std::make_unique<Hotkey> ([this] { keyTapped(); }, [this] { altReleased(); },
                                       [this] { if (! shown) return false; juce::MessageManager::callAsync ([this] { escapePressed(); }); return true; });
}

QuickMenu::~QuickMenu() = default;

bool QuickMenu::altDown() { return (GetAsyncKeyState (VK_MENU) & 0x8000) != 0; }

int QuickMenu::currentIndex() const
{
    if (c.autoMode()) return 0;
    for (int i = 1; i < 5; ++i) if (kTiles[i] == c.now().kind) return i;
    return 0;
}

void QuickMenu::keyTapped()
{
    lastTap = nowMs();
    if (! shown)
    {
        show();
        focus = (currentIndex() + 1) % 5;     // the next mode is lit at once
    }
    else focus = (focus + 1) % 5;
    keyboardUsed = true;
    autoApply = true;
    startTimerHz (120);
    repaint();
}

// Releasing Alt does nothing on purpose: a quick tap must leave time to pick with the mouse. The lit mode is\r
// applied by the countdown, or at once by a click.
void QuickMenu::altReleased() {}

void QuickMenu::escapePressed() { hide(); }

void QuickMenu::show()
{
    // Centre on the monitor with the mouse (where the user is looking). The window is not activated.
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());
    const auto area = display ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1920, 1080);
    setCentrePosition (area.getCentreX(), area.getY() + area.getHeight() * 2 / 5);
    focus = currentIndex();
    keyboardUsed = false;
    autoApply = false;
    mouseInside = false;
    outSince = nowMs();
    lastMouse = juce::Desktop::getMousePosition();
    flash = 0;
    applying = false;
    shownAt = nowMs();
    glow.fill (0);
    if (! isOnDesktop())
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIsSemiTransparent | juce::ComponentPeer::windowIgnoresKeyPresses);
    setVisible (true);
    toFront (false);
    shown = true;
    startTimerHz (120);
}

void QuickMenu::hide()
{
    shown = false;
    autoApply = false;
    startTimerHz (120);
}

void QuickMenu::choose (int index)
{
    if (applying) return;
    applying = true;
    focus = index;
    autoApply = false;
    if (kTiles[index] == Kind::Unknown) c.setAuto(); else c.pick (kTiles[index]);
    flash = 1.0f;
    juce::Timer::callAfterDelay (200, [safe = juce::Component::SafePointer<QuickMenu> (this)] { if (safe) safe->hide(); });
    startTimerHz (120);
}

void QuickMenu::timerCallback()
{
    {
        const double n = nowMs();
        frameDt = lastFrame > 0 ? juce::jlimit (0.001f, 0.05f, (float) ((n - lastFrame) / 1000.0)) : 1.0f / 60.0f;
        lastFrame = n;
    }
    const float target = shown ? 1.0f : 0.0f;
    const float step = dt() * (shown ? 6.5f : 9.0f);
    anim = anim < target ? std::min (target, anim + step) : std::max (target, anim - step);
    for (size_t i = 0; i < glow.size(); ++i)
    {
        const float t = (int) i == focus && shown ? 1.0f : 0.0f;
        glow[i] += (t - glow[i]) * rate (dt(), 20.0f);
    }
    flash = std::max (0.0f, flash - dt() * 3.0f);
    // The menu lives while Alt is held: letting go applies the lit mode and closes it (a quick tap shows it for a moment).
    if (shown && ! applying && ! altDown() && nowMs() - shownAt > 260) choose (focus);
    if (! shown && anim <= 0.0f)
    {
        stopTimer();
        lastFrame = 0;
        if (isOnDesktop()) removeFromDesktop();   // the window is made anew for every use (reusing a hidden layered window crashed)
    }
    setAlpha (juce::jlimit (0.0f, 1.0f, anim * 1.2f));
    repaint();
}

juce::Rectangle<float> QuickMenu::tile (int i) const
{
    const float w = 108.0f, gap = 10.0f, total = 5 * w + 4 * gap;
    const float x0 = ((float) getWidth() - total) / 2;
    return { x0 + (float) i * (w + gap), (float) kMargin + 62.0f, w, 112.0f };
}

int QuickMenu::tileAt (juce::Point<int> p) const
{
    for (int i = 0; i < 5; ++i) if (tile (i).expanded (4.0f, 4.0f).contains (p.toFloat())) return i;
    return -1;
}

void QuickMenu::paint (juce::Graphics& g)
{
    const float e = easeOut (anim);
    const auto bounds = getLocalBounds().toFloat();
    // the whole panel grows from 94 % and settles
    g.addTransform (juce::AffineTransform::scale (0.94f + 0.06f * e, 0.94f + 0.06f * e, bounds.getCentreX(), bounds.getCentreY()));
    auto r = bounds.reduced ((float) kMargin);
    juce::Path panel;
    panel.addRoundedRectangle (r, 18.0f);
    if (shadowCache.getWidth() != getWidth() || shadowCache.getHeight() != getHeight())
    {
        shadowCache = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics gs (shadowCache);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 22, { 0, 6 }).drawForPath (gs, panel);
    }
    g.drawImageAt (shadowCache, 0, 0);
    g.setColour (juce::Colour (0xf20d0f13));
    g.fillPath (panel);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (panel, juce::PathStrokeType (1.0f));

    g.setColour (col::text);
    g.setFont (font (16.0f, 600));
    g.drawText (L ("Режим звука", "Sound mode"), juce::Rectangle<float> (r.getX() + 24, r.getY() + 14, 300, 28), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (13.0f));
    const juce::String hint = c.autoMode() ? L ("сейчас авто: ", "auto now: ") + kindName (c.now().kind).toLowerCase() : L ("выбрано вручную", "picked by hand");
    g.drawText (hint, juce::Rectangle<float> (r.getX() + 24, r.getY() + 14, r.getWidth() - 48, 28), juce::Justification::centredRight);

    for (int i = 0; i < 5; ++i)
    {
        const Kind k = kTiles[i];
        // tiles come in one after another
        const float te = easeOut (juce::jlimit (0.0f, 1.0f, (anim - 0.08f * (float) i) / 0.6f));
        auto t = tile (i).translated (0, (1.0f - te) * 12.0f);
        const juce::Colour kc = kindColour (k);
        const bool current = k == Kind::Unknown ? c.autoMode() : (! c.autoMode() && c.now().kind == k);
        const float lit = glow[(size_t) i];
        if (flash > 0 && i == focus) t = t.expanded (flash * 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.03f * te));
        g.fillRoundedRectangle (t, 14.0f);
        if (lit > 0.02f)
        {
            juce::Path tp;
            tp.addRoundedRectangle (t, 14.0f);
            softGlow (g, tp, kc, 0.35f * lit * te, 12.0f);
            g.setColour (col::bg.withAlpha (lit * te));
            g.fillPath (tp);
            g.setGradientFill (juce::ColourGradient (kc.withAlpha (0.02f * lit), t.getX(), t.getY(), kc.withAlpha (0.30f * lit), t.getX(), t.getBottom(), false));
            g.fillPath (tp);
            g.setColour (kc.withAlpha (0.8f * lit));
            g.strokePath (tp, juce::PathStrokeType (1.3f));
        }
        g.setColour ((current ? kc.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.09f)).withMultipliedAlpha (te * (1.0f - lit * 0.6f)));
        g.drawRoundedRectangle (t.reduced (0.5f), 14.0f, 1.0f);
        if (lit > 0.02f) glowDot (g, { t.getCentreX(), t.getY() + 35.0f }, 11.0f, kc, 0.55f * lit * te);
        const juce::Colour ic = col::dim.interpolatedWith (kc.brighter (0.15f), juce::jmax (lit, current ? 0.7f : 0.0f));
        drawKindIcon (g, k, juce::Rectangle<float> (t.getCentreX() - 17, t.getY() + 18, 34, 34), ic.withMultipliedAlpha (te), 2.0f);
        g.setColour (col::dim.interpolatedWith (col::text, lit).withMultipliedAlpha (te));
        g.setFont (font (14.0f, 600));
        g.drawText (kindName (k), t.withTop (t.getY() + 62).withHeight (22), juce::Justification::centred);
    }
}

void QuickMenu::mouseMove (const juce::MouseEvent& e)
{
    mouseInside = true;
    // the window can appear under a still mouse: only a real movement takes the choice away from the keyboard
    const auto now = juce::Desktop::getMousePosition();
    if (now.getDistanceFrom (lastMouse) >= 4)
    {
        lastMouse = now;
        autoApply = false;          // nothing is applied by itself any more
        keyboardUsed = false;
        const int i = tileAt (e.getPosition());
        if (i >= 0) focus = i;
    }
}

void QuickMenu::mouseUp (const juce::MouseEvent& e)
{
    const int i = tileAt (e.getPosition());
    if (i >= 0) choose (i);
}
} // namespace rv::ui

