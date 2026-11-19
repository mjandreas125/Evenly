// Records the singing band of what is playing at its place in the song, for AlignTest: "position logEnergy" per 50 ms block.
// The place comes from Windows "Now playing", the same way the app takes it. Makes no sound.
// bandlog.exe out.txt [seconds]
#include "../app/VoiceCapture.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace wmc = winrt::Windows::Media::Control;

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("bandlog out.txt [seconds]\n"); return 1; }
    winrt::init_apartment();
    const int secs = argc > 2 ? std::atoi (argv[2]) : 120;
    auto mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    std::FILE* out = std::fopen (argv[1], "w");
    auto& cap = rv::VoiceCapture::get();
    std::vector<std::pair<double, float>> band;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    double pos = 0; auto posAt = clk::now(); bool playing = false;
    auto poll = [&]
    {
        auto s = mgr.GetCurrentSession();
        for (const auto& x : mgr.GetSessions())
            if (x.GetPlaybackInfo().PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) { s = x; break; }
        if (! s) { playing = false; return; }
        playing = s.GetPlaybackInfo().PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        const auto tl = s.GetTimelineProperties();
        pos = std::chrono::duration<double> (tl.Position()).count();
        if (playing) pos += std::chrono::duration<double> (winrt::clock::now() - tl.LastUpdatedTime()).count();
        posAt = clk::now();
    };
    poll();
    auto nextPoll = clk::now() + std::chrono::seconds (1);
    int n = 0;
    while (clk::now() - t0 < std::chrono::seconds (secs))
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (16));
        if (clk::now() >= nextPoll) { poll(); nextPoll = clk::now() + std::chrono::seconds (1); }
        cap.features();                 // keeps the listening going
        cap.takeSingingBand (band);
        if (! playing) continue;
        const double now = std::chrono::duration<double> (clk::now() - posAt).count();
        for (const auto& b : band) { std::fprintf (out, "%.3f %.3f\n", pos + now - b.first, b.second); ++n; }
    }
    std::fclose (out);
    std::printf ("%d blocks, last position %.1f s\n", n, pos);
    std::fflush (stdout);
    std::_Exit (0);
}
