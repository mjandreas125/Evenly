#pragma once
// Output devices: names, which one is the Windows default, whether Rovno is installed on each,
// installing/removing it (elevated helper), making a device the default, and spotting FxSound.
#include <string>
#include <vector>

namespace rv
{
struct OutputDevice
{
    std::wstring guid;           // {C9E05C26-...}  (upper case: used for registry keys and settings)
    std::wstring id;             // "{0.0.0.00000000}.{c9e05c26-...}" spelled exactly as Windows spells it
    std::wstring name;           // "ASUS MX34VQ"
    std::wstring adapter;        // "AMD High Definition Audio Device"
    bool isDefault = false;
    bool rovnoInstalled = false;
    bool slotTaken = false;      // another program's effect sits where Rovno would go
    bool isFxSound = false;      // FxSound's (DFX) virtual device
    bool isVirtual = false;      // a software device (voice changers, mixers, cables)
    bool digital = false;        // HDMI / DisplayPort / S/PDIF: may fall asleep when idle
    bool headphones = false;     // headphones or a headset (also Bluetooth ones): gentler loudness
    bool plugged = true;         // false while e.g. the monitor is off; it comes back by itself
};

namespace devices
{
std::vector<OutputDevice> list();
std::wstring defaultGuid();
// The endpoint id WASAPI expects. Windows spells the guid in lower case and compares the string
// as it is: a guid in upper case makes IAudioClient::Initialize fail with DEVICE_IN_USE as soon as
// anything else is already playing to that output. Always build the id through this.
std::wstring endpointId (const std::wstring& guid);
bool setDefault (const std::wstring& guid);                     // all roles (console, media, calls)
// Runs "rovno_ctl enable|disable <guid>" elevated (Windows asks for permission) and waits.
// Restarts the Windows audio service, which silences all sound for about a second.
bool installElevated (const std::wstring& guid, bool install, std::wstring& error);
bool fxSoundRunning();
std::wstring deviceUsedBy (const wchar_t* exe);                 // the output a program plays to, e.g. dfx.exe
std::wstring helperPath();                                      // rovno_ctl.exe next to the app
} // namespace devices
} // namespace rv
