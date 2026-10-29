#include "QuickMenu.h"
#include <windows.h>

namespace rv::ui
{
namespace
{
const Kind kTiles[5] = { Kind::Unknown, Kind::Music, Kind::Speech, Kind::Movie, Kind::Game };
}

// A message-only window that owns the global hotkey (Windows sends WM_HOTKEY to it).
struct QuickMenu::Hotkey
{
    explicit Hotkey (std::function<void()> cb) : onPress (std::move (cb))
    {
        WNDCLASSW wc {};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW (nullptr);
        wc.lpszClassName = L"RovnoHotkeyWindow";
        RegisterClassW (&wc);
        hwnd = CreateWindowExW (0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        SetWindowLongPtrW (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (this));
        // Ctrl+Alt+` (the key left of 1; "ё" on Russian layouts). Ctrl+` alone belongs to VS Code.
        ok = RegisterHotKey (hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_OEM_3) != 0;
    }
    ~Hotkey()
    {
        UnregisterHotKey (hwnd, 1);
        DestroyWindow (hwnd);
    }
    static LRESULT CALLBACK proc (HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        if (msg == WM_HOTKEY)
            if (auto* self = reinterpret_cast<Hotkey*> (GetWindowLongPtrW (h, GWLP_USERDATA))) self->onPress();
        return DefWindowProcW (h, msg, w, l);
    }
    std::function<void()> onPress;
    HWND hwnd = nullptr;
    bool ok = false;
};

QuickMenu::QuickMenu (Controller& controller) : c (controller)
{
    setSize (620, 196);
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);
    hotkey = std::make_unique<Hotkey> ([this] { toggle(); });
}

QuickMenu::~QuickMenu() = default;

void QuickMenu::toggle()
{
    if (isOnDesktop() && target > 0) hide(); else show();
}

void QuickMenu::show()
{
    // Centre on the monitor with the mouse (where the user is looking).
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());
    const auto area = display ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1920, 1080);
    setCentrePosition (area.getCentreX(), area.getY() + area.getHeight() * 2 / 5);
    focus = c.autoMode() ? 0 : [this] { for (int i = 1; i < 5; ++i) if (kTiles[i] == c.now().kind) return i; return 0; }();
    if (! isOnDesktop()) addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIsSemiTransparent);
    setVisible (true);
    toFront (true);
    if (auto* peer = getPeer()) SetForegroundWindow ((HWND) peer->getNativeHandle());
    grabKeyboardFocus();
    target = 1;
    startTimerHz (60);
}

void QuickMenu::hide()
{
    target = 0;
    startTimerHz (60);
}

void QuickMenu::timerCallback()
{
    alpha += (target - alpha) * 0.3f;
    if (std::abs (target - alpha) < 0.02f)
    {
        alpha = target;
        stopTimer();
        if (target == 0 && isOnDesktop()) removeFromDesktop();
    }
    setAlpha (alpha);
    repaint();
}

juce::Rectangle<float> QuickMenu::tile (int i) const
{
    const float w = 108.0f, gap = 10.0f, total = 5 * w + 4 * gap;
    const float x0 = ((float) getWidth() - total) / 2;
    return { x0 + (float) i * (w + gap), 58.0f, w, 116.0f };
}

int QuickMenu::tileAt (juce::Point<int> p) const
{
    for (int i = 0; i < 5; ++i) if (tile (i).contains (p.toFloat())) return i;
    return -1;
}

void QuickMenu::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xf20d0f13));
    g.fillRoundedRectangle (r, 18.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r, 18.0f, 1.0f);
    g.setColour (col::text);
    g.setFont (font (16.0f, 600));
    g.drawText (L ("Режим звука", "Sound mode"), juce::Rectangle<float> (26, 16, 300, 26), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (13.0f));
    const juce::String hint = c.autoMode() ? L ("сейчас авто: ", "auto now: ") + kindName (c.now().kind).toLowerCase() : L ("выбрано вручную", "picked by hand");
    g.drawText (hint, juce::Rectangle<float> (26, 16, (float) getWidth() - 52, 26), juce::Justification::centredRight);
    for (int i = 0; i < 5; ++i)
    {
        const Kind k = kTiles[i];
        auto t = tile (i);
        const juce::Colour kc = kindColour (k);
        const bool current = k == Kind::Unknown ? c.autoMode() : (! c.autoMode() && c.now().kind == k);
        const bool lit = i == focus;
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (t, 14.0f);
        if (lit)
        {
            juce::Path tp;
            tp.addRoundedRectangle (t, 14.0f);
            juce::DropShadow (kc.withAlpha (0.35f), 14, { 0, 0 }).drawForPath (g, tp);
            g.setColour (col::bg);
            g.fillPath (tp);
            g.setGradientFill (juce::ColourGradient (kc.withAlpha (0.02f), t.getX(), t.getY(), kc.withAlpha (0.30f), t.getX(), t.getBottom(), false));
            g.fillPath (tp);
            g.setColour (kc.withAlpha (0.8f));
            g.strokePath (tp, juce::PathStrokeType (1.3f));
            juce::Path base;
            base.startNewSubPath (t.getX() + 16, t.getBottom() - 1.0f);
            base.lineTo (t.getRight() - 16, t.getBottom() - 1.0f);
            glowStroke (g, base, kc.brighter (0.2f), 2.0f, 1.3f);
        }
        else
        {
            g.setColour (current ? kc.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.09f));
            g.drawRoundedRectangle (t.reduced (0.5f), 14.0f, 1.0f);
        }
        if (lit) glowDot (g, { t.getCentreX(), t.getY() + 35.0f }, 11.0f, kc, 0.55f);
        drawKindIcon (g, k, juce::Rectangle<float> (t.getCentreX() - 17, t.getY() + 18, 34, 34), lit || current ? kc.brighter (0.15f) : col::dim, 2.0f);
        g.setColour (lit ? col::text : col::dim);
        g.setFont (font (14.0f, 600));
        g.drawText (kindName (k), t.withTop (t.getY() + 62).withHeight (22), juce::Justification::centred);
        g.setColour (col::faint);
        g.setFont (font (12.0f, 500));
        g.drawText (juce::String (i + 1), t.withTop (t.getY() + 86).withHeight (18), juce::Justification::centred);
    }
}

bool QuickMenu::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { hide(); return true; }
    if (code >= '1' && code <= '5') { choose (code - '1'); return true; }
    if (code == juce::KeyPress::leftKey) { focus = (focus + 4) % 5; repaint(); return true; }
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::tabKey) { focus = (focus + 1) % 5; repaint(); return true; }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey) { choose (focus); return true; }
    return false;
}

void QuickMenu::mouseMove (const juce::MouseEvent& e)
{
    const int i = tileAt (e.getPosition());
    if (i >= 0 && i != focus) { focus = i; repaint(); }
}

void QuickMenu::mouseUp (const juce::MouseEvent& e)
{
    const int i = tileAt (e.getPosition());
    if (i >= 0) choose (i);
}

void QuickMenu::choose (int index)
{
    focus = index;
    if (kTiles[index] == Kind::Unknown) c.setAuto(); else c.pick (kTiles[index]);
    repaint();
    juce::Timer::callAfterDelay (180, [safe = juce::Component::SafePointer<QuickMenu> (this)] { if (safe) safe->hide(); });
}
} // namespace rv::ui
