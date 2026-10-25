#pragma once
// Output devices: names, which one is the Windows default, whether Rovno is installed on each,
// installing/removing it (elevated helper), making a device the default, and spotting FxSound.
#include <string>
#include <vector>

namespace rv
{
struct OutputDevice
{
    std::wstring guid;           // {C9E05C26-...}
    std::wstring name;           // "ASUS MX34VQ"
    std::wstring adapter;        // "AMD High Definition Audio Device"
    bool isDefault = false;
    bool rovnoInstalled = false;
    bool slotTaken = false;      // another program's effect sits where Rovno would go
    bool isFxSound = false;      // FxSound's (DFX) virtual device
    bool isVirtual = false;      // a software device (voice changers, mixers, cables)
    bool digital = false;        // HDMI / DisplayPort / S/PDIF: may fall asleep when idle
    bool plugged = true;         // false while e.g. the monitor is off; it comes back by itself
};

namespace devices
{
std::vector<OutputDevice> list();
std::wstring defaultGuid();
bool setDefault (const std::wstring& guid);                     // all roles (console, media, calls)
// Runs "rovno_ctl enable|disable <guid>" elevated (Windows asks for permission) and waits.
// Restarts the Windows audio service, which silences all sound for about a second.
bool installElevated (const std::wstring& guid, bool install, std::wstring& error);
bool fxSoundRunning();
std::wstring deviceUsedBy (const wchar_t* exe);                 // the output a program plays to, e.g. dfx.exe
std::wstring helperPath();                                      // rovno_ctl.exe next to the app
} // namespace devices
} // namespace rv
