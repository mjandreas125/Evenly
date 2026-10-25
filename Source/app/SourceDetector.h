#pragma once
// What is playing right now: which program makes sound on the device (audio sessions), what
// Windows "Now playing" says (title, artist/channel, music or video), and that program's window
// title. Plain Win32/WinRT, no JUCE; poll() is called from a background thread.
#include <cstdint>
#include <string>
#include <vector>

namespace rv
{
struct PlayingApp
{
    uint32_t pid = 0;
    std::wstring exe;            // "chrome.exe"
    std::wstring windowTitle;    // "Some video - YouTube - Google Chrome"
    float peak = 0;              // current level of this program's sound (0..1)
};

struct NowPlaying
{
    bool present = false;
    std::wstring appId;          // SMTC source app, e.g. "chrome.exe", "Spotify.exe"
    std::wstring title, artist, album;
    int playbackType = 0;        // 0 unknown, 1 music, 2 video, 3 image
    bool playing = false;
};

struct SourceSnapshot
{
    std::vector<PlayingApp> apps;    // programs with sound right now, loudest first
    NowPlaying media;
};

class SourceDetector
{
public:
    SourceDetector();
    ~SourceDetector();
    // endpointId: "{0.0.0.00000000}.{guid}" of the output device.
    SourceSnapshot poll (const std::wstring& endpointId);
private:
    struct Impl;
    Impl* impl;
};
} // namespace rv
