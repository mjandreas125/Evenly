// Prints what the source detector sees, once a second: rovno_detect [seconds]
#include "../app/SourceDetector.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>

static std::string u8s (const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w.c_str(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) n, '\0');
    WideCharToMultiByte (CP_UTF8, 0, w.c_str(), (int) w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

int main (int argc, char** argv)
{
    SetConsoleOutputCP (CP_UTF8);
    const int secs = argc > 1 ? std::atoi (argv[1]) : 5;
    rv::SourceDetector d;
    for (int i = 0; i < secs; ++i)
    {
        const auto s = d.poll (L"");
        std::printf ("--- %d s\n", i);
        for (const auto& a : s.apps)
            std::printf ("  app %-22s pid %-6u peak %.3f  window: %s\n", u8s (a.exe).c_str(), a.pid, a.peak, u8s (a.windowTitle).c_str());
        if (s.media.present)
            std::printf ("  now playing [%s] %s: \"%s\" by \"%s\" album \"%s\" type %d\n", u8s (s.media.appId).c_str(),
                         s.media.playing ? "playing" : "paused", u8s (s.media.title).c_str(), u8s (s.media.artist).c_str(),
                         u8s (s.media.album).c_str(), s.media.playbackType);
        else std::printf ("  now playing: nothing\n");
        std::this_thread::sleep_for (std::chrono::seconds (1));
    }
    return 0;
}
