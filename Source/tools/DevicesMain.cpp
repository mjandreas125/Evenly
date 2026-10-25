// Prints what the app sees in the output devices: rovno_devices
#include "../app/Devices.h"
#include <windows.h>
#include <cstdio>
#include <string>

static std::string u8s (const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w.c_str(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) n, '\0');
    WideCharToMultiByte (CP_UTF8, 0, w.c_str(), (int) w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

int main()
{
    SetConsoleOutputCP (CP_UTF8);
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    for (const auto& d : rv::devices::list())
        std::printf ("%s  %-28s %-42s default=%d rovno=%d taken=%d fx=%d virtual=%d digital=%d plugged=%d\n", u8s (d.guid).c_str(),
                     u8s (d.name).c_str(), u8s (d.adapter).c_str(), d.isDefault, d.rovnoInstalled, d.slotTaken, d.isFxSound, d.isVirtual,
                     d.digital, d.plugged);
    std::printf ("default: %s\ndfx.exe plays to: %s\nFxSound running: %d\n", u8s (rv::devices::defaultGuid()).c_str(),
                 u8s (rv::devices::deviceUsedBy (L"dfx.exe")).c_str(), rv::devices::fxSoundRunning());
    return 0;
}
