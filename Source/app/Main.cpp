// Rovno: the app. One instance, lives in the tray; closing the window keeps the sound processing.
//   Rovno.exe            open the window
//   Rovno.exe --tray     start hidden in the tray (autostart)
//   Rovno.exe --snapshot out.png [--eq] [--scale 1.3]   render the window to a PNG and quit (for checking the look)
#include "MainComponent.h"
#include "QuickMenu.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <dwmapi.h>
#include <windows.h>

namespace rv::ui
{
// Interface scale. Automatic (0): the window is sized to fit the monitor it is on (its work area, so the
// taskbar is respected), from 65 % on a small laptop screen up to 130 % on a big one, and it is fitted again
// when the window is moved to another monitor. Otherwise a percentage the user picked in the More menu.
// The percentage is of the design size in real pixels (Windows' own display scaling is taken into account).
juce::File scaleFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("ui-scale.txt"); }
int savedScale() { const auto f = scaleFile(); return f.existsAsFile() ? f.loadFileAsString().trim().getIntValue() : 0; }
void saveScale (int percent) { const auto f = scaleFile(); f.getParentDirectory().createDirectory(); f.replaceWithText (juce::String (percent)); }

struct MonitorSpace { float workW = 1920, workH = 1040, dpi = 1.0f; };
MonitorSpace monitorSpace (HWND hwnd)
{
    MonitorSpace s;
    HMONITOR mon = hwnd ? MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint (POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi {};
    mi.cbSize = sizeof (mi);
    if (mon != nullptr && GetMonitorInfoW (mon, &mi))
    {
        s.workW = (float) (mi.rcWork.right - mi.rcWork.left);
        s.workH = (float) (mi.rcWork.bottom - mi.rcWork.top);
    }
    const UINT dpi = hwnd ? GetDpiForWindow (hwnd) : GetDpiForSystem();
    if (dpi > 0) s.dpi = (float) dpi / 96.0f;
    return s;
}

// The scale (in real pixels per design pixel) at which the tallest layout (with the banner) and the title bar fit.
float fitScale (const MonitorSpace& s)
{
    constexpr float designW = 1120.0f, designH = 846.0f;
    const float chrome = 48.0f * s.dpi, side = 16.0f * s.dpi;
    const float t = std::min ({ (s.workH * 0.96f - chrome) / designH, (s.workW * 0.96f - side) / designW, 1.30f });
    return juce::jlimit (0.65f, 1.30f, std::round (t * 20.0f) / 20.0f);
}

void applyScale (int percent, HWND hwnd = nullptr)
{
    const auto monitor = monitorSpace (hwnd);
    const float total = percent >= 50 ? (float) percent / 100.0f : fitScale (monitor);
    juce::Desktop::getInstance().setGlobalScaleFactor (total / monitor.dpi);   // JUCE multiplies it by the display's own scaling
}

// JUCE popup menus appear at once; this fades every new menu window in (submenus and the tray menu too).
class MenuFader : private juce::Timer
{
public:
    MenuFader() { startTimer (15); }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* c = desktop.getComponent (i))
                if (c->getName() == "menu" && c->isVisible() && ! c->getProperties().contains ("evenlyFaded"))   // not one that is closing: fadeIn would show it again
                {
                    c->getProperties().set ("evenlyFaded", true);
                    desktop.getAnimator().fadeIn (c, 120);
                }
    }
};

class MainWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit MainWindow (Controller& c) : juce::DocumentWindow ("Rovno", col::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
        setName (appName());
        setUsingNativeTitleBar (true);
        auto* content = new MainComponent (c);
        content->onScale = [this] (int percent)
        {
            saveScale (percent);
            applyScale (percent, hwnd());
            refit();
            centreWithSize (getWidth(), getHeight());
        };
        setContentOwned (content, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setIcon (juce::ImageCache::getFromMemory (BinaryData::icon256_png, BinaryData::icon256_pngSize));
    }
    void closeButtonPressed() override { setVisible (false); }
    // Moved to another monitor: fit the window to that one (once the move has stopped).
    void moved() override { juce::DocumentWindow::moved(); if (savedScale() == 0) startTimer (450); }
    // A new scale: JUCE keeps the window's size in real pixels and changes its logical size, so the content has to be
    // given its design size again to make the window shrink or grow.
    void refit()
    {
        if (auto* content = dynamic_cast<MainComponent*> (getContentComponent()))
            setContentComponentSize (MainComponent::kWidth, content->preferredHeight());
    }
    // Never leave the window half off the screen after it has changed size (real pixels, the monitor's work area).
    void keepInsideWorkArea()
    {
        HWND h = hwnd();
        if (h == nullptr) return;
        RECT rc {};
        MONITORINFO mi {};
        mi.cbSize = sizeof (mi);
        if (! GetWindowRect (h, &rc) || ! GetMonitorInfoW (MonitorFromWindow (h, MONITOR_DEFAULTTONEAREST), &mi)) return;
        const int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
        const int x = juce::jlimit ((int) mi.rcWork.left, std::max ((int) mi.rcWork.left, (int) mi.rcWork.right - w), (int) rc.left);
        const int y = juce::jlimit ((int) mi.rcWork.top, std::max ((int) mi.rcWork.top, (int) mi.rcWork.bottom - hgt), (int) rc.top);
        if (x != rc.left || y != rc.top) SetWindowPos (h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HWND hwnd() { return getPeer() ? (HWND) getPeer()->getNativeHandle() : nullptr; }
    void timerCallback() override
    {
        stopTimer();
        if (savedScale() != 0 || ! isVisible()) return;
        const float before = juce::Desktop::getInstance().getGlobalScaleFactor();
        applyScale (0, hwnd());
        if (std::abs (juce::Desktop::getInstance().getGlobalScaleFactor() - before) > 0.004f)
        {
            refit();
            juce::Timer::callAfterDelay (120, [safe = juce::Component::SafePointer<MainWindow> (this)] { if (safe) safe->keepInsideWorkArea(); });
        }
    }
    void visibilityChanged() override
    {
        // Dark title bar to match the window (Windows 10 2004+ / 11).
        if (auto* peer = getPeer())
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute ((HWND) peer->getNativeHandle(), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof (dark));
        }
    }
};

class Tray : public juce::SystemTrayIconComponent
{
public:
    Tray (Controller& controller, std::function<void()> toggleWindow) : c (controller), toggle (std::move (toggleWindow))
    {
        const auto icon = juce::ImageCache::getFromMemory (BinaryData::icon32_png, BinaryData::icon32_pngSize);
        setIconImage (icon, icon);
        setIconTooltip (appName());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) { toggle(); return; }
        juce::PopupMenu m;
        m.addItem (1, L ("Открыть", "Open"));
        m.addSeparator();
        m.addItem (10, kindName (Kind::Unknown), true, c.autoMode());
        for (Kind k : { Kind::Music, Kind::Speech, Kind::Movie, Kind::Game })
            m.addItem (11 + (int) k, kindName (k), true, ! c.autoMode() && c.now().kind == k);
        m.addSeparator();
        m.addItem (2, L ("Обработка включена", "Processing on"), true, c.enabled());
        m.addSeparator();
        m.addItem (3, L ("Выход", "Quit"));
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1) toggle();
            else if (r == 2) c.setEnabled (! c.enabled());
            else if (r == 3) juce::JUCEApplication::getInstance()->systemRequestedQuit();
            else if (r == 10) c.setAuto();
            else if (r >= 11 && r <= 14) c.pick ((Kind) (r - 11));
        });
    }
private:
    Controller& c;
    std::function<void()> toggle;
};
} // namespace rv::ui

class RovnoApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Rovno"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        controller = std::make_unique<rv::Controller>();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // the tray menu gets the same look
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const int snap = args.indexOf ("--snapshot");
        if (snap >= 0)
        {
            // Let the controller see the effect and what is playing, then draw and quit.
            const int wait = args.contains ("--wait") ? args[args.indexOf ("--wait") + 1].getIntValue() * 1000 : 2600;
            rv::ui::MainComponent::hideBanner = args.contains ("--clean");
            rv::ui::MainComponent::demoData = args.contains ("--demo");
            const float snapScale = args.contains ("--scale") ? args[args.indexOf ("--scale") + 1].getFloatValue() : 1.0f;
            juce::Timer::callAfterDelay (wait, [this, snapScale, path = args[snap + 1].unquoted(), eq = args.contains ("--eq"), menu = args.contains ("--menu")]
            {
                juce::Image img;
                if (menu)
                {
                    rv::ui::QuickMenu m (*controller);
                    img = m.createComponentSnapshot (m.getLocalBounds(), true, 1.0f);
                }
                else
                {
                    rv::ui::MainComponent comp (*controller);
                    if (eq) comp.setEqOpen (true);
                    img = comp.createComponentSnapshot (comp.getLocalBounds(), true, snapScale);
                }
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (img, out);
                out.flush();
                quit();
            });
            return;
        }
        rv::ui::applyScale (rv::ui::savedScale());
        window = std::make_unique<rv::ui::MainWindow> (*controller);
        quickMenu = std::make_unique<rv::ui::QuickMenu> (*controller);
        tray = std::make_unique<rv::ui::Tray> (*controller, [this] { toggleWindow(); });
        rv::ui::languageListener() = [this] { if (tray) tray->setIconTooltip (rv::ui::appName()); };
        window->setVisible (! args.contains ("--tray"));
    }

    void shutdown() override
    {
        tray.reset();
        quickMenu.reset();
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        controller.reset();
    }

    void anotherInstanceStarted (const juce::String&) override { showWindow(); }
    void systemRequestedQuit() override { quit(); }

private:
    void showWindow()
    {
        if (! window) return;
        window->setVisible (true);
        window->toFront (true);
    }
    void toggleWindow()
    {
        if (window && window->isVisible() && window->isActiveWindow()) window->setVisible (false);
        else showWindow();
    }

    rv::ui::RovnoLookAndFeel lookAndFeel;
    rv::ui::MenuFader menuFader;
    std::unique_ptr<rv::Controller> controller;
    std::unique_ptr<rv::ui::MainWindow> window;
    std::unique_ptr<rv::ui::QuickMenu> quickMenu;
    std::unique_ptr<rv::ui::Tray> tray;
};

START_JUCE_APPLICATION (RovnoApplication)
