// Rovno: the app. One instance, lives in the tray; closing the window keeps the sound processing.
//   Rovno.exe            open the window
//   Rovno.exe --tray     start hidden in the tray (autostart)
//   Rovno.exe --snapshot out.png [--eq]   render the window to a PNG and quit (for checking the look)
#include "MainComponent.h"
#include "QuickMenu.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <dwmapi.h>
#include <windows.h>

namespace rv::ui
{
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (Controller& c) : juce::DocumentWindow ("Rovno", col::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
        setName (L ("Ровно", "Rovno"));
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (c), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setIcon (juce::ImageCache::getFromMemory (BinaryData::icon256_png, BinaryData::icon256_pngSize));
    }
    void closeButtonPressed() override { setVisible (false); }
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
        setIconTooltip (L ("Ровно", "Rovno"));
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
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const int snap = args.indexOf ("--snapshot");
        if (snap >= 0)
        {
            // Let the controller see the effect and what is playing, then draw and quit.
            juce::Timer::callAfterDelay (2600, [this, path = args[snap + 1].unquoted(), eq = args.contains ("--eq"), menu = args.contains ("--menu")]
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
                    img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
                }
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (img, out);
                out.flush();
                quit();
            });
            return;
        }
        window = std::make_unique<rv::ui::MainWindow> (*controller);
        quickMenu = std::make_unique<rv::ui::QuickMenu> (*controller);
        tray = std::make_unique<rv::ui::Tray> (*controller, [this] { toggleWindow(); });
        window->setVisible (! args.contains ("--tray"));
    }

    void shutdown() override
    {
        tray.reset();
        quickMenu.reset();
        window.reset();
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

    std::unique_ptr<rv::Controller> controller;
    std::unique_ptr<rv::ui::MainWindow> window;
    std::unique_ptr<rv::ui::QuickMenu> quickMenu;
    std::unique_ptr<rv::ui::Tray> tray;
};

START_JUCE_APPLICATION (RovnoApplication)
