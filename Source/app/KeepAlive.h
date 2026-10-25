#pragma once
// Keeps an output awake by playing digital silence to it. HDMI/DisplayPort audio (monitors, TVs)
// powers down after a few seconds of idle and then swallows the start of the next sound; an open
// stream prevents that. Runs on its own thread; does not keep the PC from sleeping.
#include <atomic>
#include <string>
#include <thread>

namespace rv
{
class KeepAlive
{
public:
    ~KeepAlive() { stop(); }
    void start (const std::wstring& endpointGuid);
    void stop();
    bool running() const { return active.load(); }
private:
    void run (std::wstring guid);
    std::thread worker;
    std::atomic<bool> quit { false }, active { false };
};
} // namespace rv
