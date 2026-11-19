// The colours of what is on the screen (the video being watched), as three palette colours for the
// picture. A small thread looks at the monitor a few times a second: the screen is shrunk to a 64 x 36
// thumbnail, every pixel that has colour votes for its hue, the strongest hues are the palette.
// Our own windows are left out: the full-screen picture is hidden from screen capture, the main
// window's rectangle is skipped.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <windows.h>
#include <juce_graphics/juce_graphics.h>

namespace rv::ui
{
class ScreenPalette
{
public:
    static ScreenPalette& get()
    {
        static ScreenPalette p;
        return p;
    }

    struct Palette
    {
        std::array<juce::Colour, 3> colours;
        float confidence = 0;   // 0 = the screen has no colour to speak of (grey, black); 1 = clearly coloured
        float brightness = 0;   // mean brightness of the screen, 0..1
    };

    // The thread runs only while some picture wants the colours (each asks for itself).
    void want (const void* who, bool on)
    {
        bool any;
        {
            std::lock_guard<std::mutex> l (lock);
            if (on) wanters.insert (who); else wanters.erase (who);
            any = ! wanters.empty();
        }
        if (any == wantedFlag.load()) return;
        wantedFlag = any;
        if (any && ! running.exchange (true))
        {
            if (worker.joinable()) worker.join();
            worker = std::thread ([this] { loop(); });
        }
    }

    // The main window: its rectangle is left out of the sample and its monitor is the one looked at.
    void setMainWindow (HWND h) { mainWindow = h; }

    bool read (Palette& out)
    {
        std::lock_guard<std::mutex> l (lock);
        if (! have) return false;
        out = latest;
        return true;
    }

    // Keeps a window out of screen capture (the full-screen picture must not sample itself).
    static void hideFromCapture (HWND h)
    {
        if (h == nullptr) return;
        constexpr DWORD WDA_EXCLUDEFROMCAPTURE_ = 0x00000011;
        SetWindowDisplayAffinity (h, WDA_EXCLUDEFROMCAPTURE_);
    }

    ~ScreenPalette()
    {
        quit = true;
        if (worker.joinable()) worker.join();
    }

private:
    ScreenPalette() = default;

    void loop()
    {
        constexpr int W = 64, H = 36;
        while (! quit)
        {
            if (! wantedFlag)
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (200));
                if (! wantedFlag) break;
                continue;
            }
            Palette p;
            if (grab (W, H, p))
            {
                std::lock_guard<std::mutex> l (lock);
                latest = p;
                have = true;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (170));
        }
        running = false;
    }

    bool grab (int W, int H, Palette& out)
    {
        HWND mw = mainWindow;
        HMONITOR mon = mw != nullptr ? MonitorFromWindow (mw, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint (POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi {};
        mi.cbSize = sizeof (mi);
        if (! GetMonitorInfoW (mon, &mi)) return false;
        const RECT mr = mi.rcMonitor;
        const int sw = mr.right - mr.left, sh = mr.bottom - mr.top;
        if (sw <= 0 || sh <= 0) return false;

        HDC screen = GetDC (nullptr);
        if (screen == nullptr) return false;
        HDC mem = CreateCompatibleDC (screen);
        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection (screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (bmp != nullptr && bits != nullptr)
        {
            HGDIOBJ old = SelectObject (mem, bmp);
            SetStretchBltMode (mem, HALFTONE);
            SetBrushOrgEx (mem, 0, 0, nullptr);
            ok = StretchBlt (mem, 0, 0, W, H, screen, mr.left, mr.top, sw, sh, SRCCOPY) != 0;
            SelectObject (mem, old);
        }
        if (ok)
        {
            // the cells under our own main window do not count
            RECT own {};
            const bool haveOwn = mw != nullptr && IsWindowVisible (mw) && GetWindowRect (mw, &own);
            analyse (static_cast<const uint8_t*> (bits), W, H, mr, haveOwn ? &own : nullptr, out);
        }
        if (bmp != nullptr) DeleteObject (bmp);
        DeleteDC (mem);
        ReleaseDC (nullptr, screen);
        return ok;
    }

    static void analyse (const uint8_t* px, int W, int H, const RECT& mon, const RECT* own, Palette& out)
    {
        constexpr int kBins = 36;
        std::array<double, kBins> weight {};
        std::array<std::array<double, 3>, kBins> sum {};
        double total = 0, bright = 0, coloured = 0;
        const int sw = mon.right - mon.left, sh = mon.bottom - mon.top;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                if (own != nullptr)
                {
                    const int sx = mon.left + (x * 2 + 1) * sw / (W * 2), sy = mon.top + (y * 2 + 1) * sh / (H * 2);
                    if (sx >= own->left && sx < own->right && sy >= own->top && sy < own->bottom) continue;
                }
                const uint8_t* p = px + ((size_t) y * (size_t) W + (size_t) x) * 4;   // BGRA
                const float b = p[0] / 255.0f, g = p[1] / 255.0f, r = p[2] / 255.0f;
                const float mx = std::max ({ r, g, b }), mn = std::min ({ r, g, b });
                const float v = mx, s = mx <= 0 ? 0 : (mx - mn) / mx;
                total += 1;
                bright += 0.2126f * r + 0.7152f * g + 0.0722f * b;
                if (s < 0.22f || v < 0.18f) continue;   // grey or nearly black: no hue to speak of
                float h;
                const float d = mx - mn;
                if (mx == r) h = std::fmod ((g - b) / d, 6.0f);
                else if (mx == g) h = (b - r) / d + 2.0f;
                else h = (r - g) / d + 4.0f;
                if (h < 0) h += 6.0f;
                const int bin = juce::jlimit (0, kBins - 1, (int) (h / 6.0f * kBins));
                const double w = (double) s * v * v;   // vivid and bright pixels speak loudest
                weight[(size_t) bin] += w;
                sum[(size_t) bin][0] += w * r;
                sum[(size_t) bin][1] += w * g;
                sum[(size_t) bin][2] += w * b;
                coloured += 1;
            }
        out.brightness = total > 0 ? (float) (bright / total) : 0.0f;
        out.confidence = total > 0 ? juce::jlimit (0.0f, 1.0f, (float) (coloured / total) * 2.5f) : 0.0f;

        // smooth the hue histogram over neighbouring bins, then take the strongest separated peaks
        std::array<double, kBins> sm {};
        for (int i = 0; i < kBins; ++i)
            sm[(size_t) i] = weight[(size_t) i] * 0.5 + (weight[(size_t) ((i + 1) % kBins)] + weight[(size_t) ((i + kBins - 1) % kBins)]) * 0.25;
        std::array<int, 3> peaks { -1, -1, -1 };
        for (int k = 0; k < 3; ++k)
        {
            double best = 0;
            for (int i = 0; i < kBins; ++i)
            {
                bool tooClose = false;
                for (int j = 0; j < k; ++j)
                {
                    const int dd = std::abs (i - peaks[(size_t) j]);
                    if (std::min (dd, kBins - dd) < 4) tooClose = true;
                }
                if (! tooClose && sm[(size_t) i] > best) { best = sm[(size_t) i]; peaks[(size_t) k] = i; }
            }
            if (best < 1e-6) break;
        }
        auto colourOf = [&] (int bin) -> juce::Colour
        {
            // average colour of the pixels in and next to the bin
            double w = 0, r = 0, g = 0, b = 0;
            for (int d = -1; d <= 1; ++d)
            {
                const size_t i = (size_t) ((bin + d + kBins) % kBins);
                w += weight[i]; r += sum[i][0]; g += sum[i][1]; b += sum[i][2];
            }
            if (w <= 0) return juce::Colours::grey;
            return juce::Colour::fromFloatRGBA ((float) (r / w), (float) (g / w), (float) (b / w), 1.0f);
        };
        auto vivid = [] (juce::Colour c)   // a palette colour has to glow on a dark picture
        {
            return juce::Colour::fromHSV (c.getHue(), juce::jlimit (0.55f, 1.0f, c.getSaturation() * 1.15f), juce::jlimit (0.62f, 0.88f, c.getBrightness() * 1.1f), 1.0f);
        };
        if (peaks[0] < 0)
        {
            out.confidence = 0;
            return;
        }
        const juce::Colour a = vivid (colourOf (peaks[0]));
        out.colours[0] = a;
        for (size_t k = 1; k < 3; ++k)
        {
            if (peaks[k] >= 0 && sm[(size_t) peaks[k]] > sm[(size_t) peaks[0]] * 0.12)
                out.colours[k] = vivid (colourOf (peaks[k]));
            else   // a one-colour scene: neighbours on the colour wheel
                out.colours[k] = juce::Colour::fromHSV (std::fmod (a.getHue() + (k == 1 ? 0.10f : 0.90f), 1.0f), a.getSaturation(), a.getBrightness(), 1.0f);
        }
    }

    std::atomic<bool> wantedFlag { false }, running { false }, quit { false };
    std::thread worker;
    std::mutex lock;
    Palette latest;
    std::set<const void*> wanters;
    bool have = false;
    std::atomic<HWND> mainWindow { nullptr };
};
}
