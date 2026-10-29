#pragma once
// Keeps an output awake by playing digital silence to it. HDMI/DisplayPort audio (monitors, TVs)
// powers down after a few seconds of idle and then swallows the start of the next sound; an open
// stream prevents that. Runs on its own thread; does not keep the PC from sleeping.
#include <atomic>
#include <string>
#include <thread>
#include <windows.h>

namespace rv
{
class KeepAlive
{
public:
    enum class Status
    {
        Off,        // not asked to run
        Holding,    // the silent stream is open: the output stays awake
        Waiting,    // the output is not there right now (monitor asleep); trying again
        Failed      // Windows refuses to open the stream; see error()
    };

    ~KeepAlive() { stop(); }
    // endpointId is the full id as Windows spells it ("{0.0.0.00000000}.{guid}"), from
    // devices::endpointId(). An id in the wrong case makes Windows fail with DEVICE_IN_USE.
    void start (const std::wstring& endpointId);
    void stop();
    bool running() const { return state.load() == Status::Holding; }
    Status status() const { return state.load(); }
    long error() const { return lastHr.load(); }        // HRESULT of the call that failed
    const char* failedAt() const { return lastStep.load(); }

private:
    void run (std::wstring id);
    bool sleepOrQuit (int ms);                           // false once stop() was asked for

    std::thread worker;
    HANDLE quitEvent = nullptr;
    std::atomic<Status> state { Status::Off };
    std::atomic<long> lastHr { 0 };
    std::atomic<const char*> lastStep { "" };
};
} // namespace rv
