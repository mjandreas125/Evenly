// rovno_ctl: installs the Rovno effect on an output device, removes it, and talks to it.
//   rovno_ctl list                      output devices and their effect slots
//   rovno_ctl install <endpoint>        (admin) put Rovno into the free endpoint-effect slot
//   rovno_ctl uninstall <endpoint>      (admin) restore what was there before
//   rovno_ctl monitor <endpoint> [sec]  print what the effect reports
//   rovno_ctl set <endpoint> key=value  change settings (also saved for the next start)
//   rovno_ctl enable|disable <endpoint> (admin) install/uninstall, then restart the audio service
//   rovno_ctl restart-audio             (admin)
//   rovno_ctl disable-all               (admin) remove from every device (uninstaller)
// <endpoint> is the GUID, e.g. {C9E05C26-B740-423A-AF2A-CA721749CFE3}.
#include "../dsp/Shared.h"
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <propsys.h>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// The interface behind the Windows Sound control panel (undocumented, stable since Windows 7).
// Writing endpoint properties through it lets the audio service see the change at once.
struct __declspec (uuid ("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : IUnknown
{
    virtual HRESULT __stdcall GetMixFormat (PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT __stdcall GetDeviceFormat (PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT __stdcall ResetDeviceFormat (PCWSTR) = 0;
    virtual HRESULT __stdcall SetDeviceFormat (PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT __stdcall GetProcessingPeriod (PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT __stdcall SetProcessingPeriod (PCWSTR, PINT64) = 0;
    virtual HRESULT __stdcall GetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT __stdcall SetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT __stdcall GetPropertyValue (PCWSTR, BOOL fxStore, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT __stdcall SetPropertyValue (PCWSTR, BOOL fxStore, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT __stdcall SetDefaultEndpoint (PCWSTR, ERole) = 0;
    virtual HRESULT __stdcall SetEndpointVisibility (PCWSTR, INT) = 0;
};
static const CLSID kPolicyConfigClient = { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };
static const PROPERTYKEY kFxEndpointClsid = { { 0xd04e05a6, 0x594b, 0x4fb6, { 0xa8, 0x0d, 0x01, 0xaf, 0x5e, 0xed, 0x7d, 0x1d } }, 7 };

namespace
{
const wchar_t* kRender = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";
const wchar_t* kEfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";
const wchar_t* kSfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5";
const wchar_t* kMfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";
const wchar_t* kEfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";
const wchar_t* kDisableSysFx = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";
const wchar_t* kDefaultMode = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
const wchar_t* kApoClsid = L"{F9DCA572-B13F-43A6-B8EE-113925372DDC}";
const wchar_t* kBackup = L"SOFTWARE\\Rovno\\Devices";
const wchar_t* kNone = L"<none>";

std::wstring readString (HKEY root, const std::wstring& path, const wchar_t* name, bool* exists = nullptr)
{
    wchar_t buf[1024] {};
    DWORD size = sizeof (buf), type = 0;
    const LONG r = RegGetValueW (root, path.c_str(), name, RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ | RRF_RT_REG_EXPAND_SZ, &type, buf, &size);
    if (exists) *exists = r == ERROR_SUCCESS;
    return r == ERROR_SUCCESS ? std::wstring (buf) : std::wstring();
}

DWORD readDword (HKEY root, const std::wstring& path, const wchar_t* name, bool* exists = nullptr)
{
    DWORD v = 0, size = sizeof (v);
    const LONG r = RegGetValueW (root, path.c_str(), name, RRF_RT_REG_DWORD, nullptr, &v, &size);
    if (exists) *exists = r == ERROR_SUCCESS;
    return v;
}

LONG writeValue (HKEY root, const std::wstring& path, const wchar_t* name, DWORD type, const void* data, DWORD bytes)
{
    HKEY k;
    LONG r = RegCreateKeyExW (root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW (k, name, 0, type, static_cast<const BYTE*> (data), bytes);
    RegCloseKey (k);
    return r;
}
LONG writeString (HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& v)
{
    return writeValue (root, path, name, REG_SZ, v.c_str(), (DWORD) ((v.size() + 1) * sizeof (wchar_t)));
}
LONG writeMulti (HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& v)
{
    std::wstring m = v + L'\0';
    return writeValue (root, path, name, REG_MULTI_SZ, m.c_str(), (DWORD) ((m.size() + 1) * sizeof (wchar_t)));
}
LONG deleteValue (HKEY root, const std::wstring& path, const wchar_t* name)
{
    HKEY k;
    LONG r = RegOpenKeyExW (root, path.c_str(), 0, KEY_SET_VALUE, &k);
    if (r != ERROR_SUCCESS) return r;
    r = RegDeleteValueW (k, name);
    RegCloseKey (k);
    return r;
}

std::string utf8 (const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w.c_str(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) n, '\0');
    WideCharToMultiByte (CP_UTF8, 0, w.c_str(), (int) w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::vector<std::wstring> endpoints()
{
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, kRender, 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    wchar_t name[256];
    for (DWORD i = 0;; ++i)
    {
        DWORD len = 256;
        if (RegEnumKeyExW (k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.push_back (name);
    }
    RegCloseKey (k);
    return out;
}

std::wstring deviceName (const std::wstring& ep)
{
    const std::wstring props = std::wstring (kRender) + L"\\" + ep + L"\\Properties";
    return readString (HKEY_LOCAL_MACHINE, props, L"{a45c254e-df1c-4efd-8020-67d146a850e0},2") + L" ("
           + readString (HKEY_LOCAL_MACHINE, props, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6") + L")";
}

int list()
{
    for (const auto& ep : endpoints())
    {
        const std::wstring base = std::wstring (kRender) + L"\\" + ep;
        const DWORD state = readDword (HKEY_LOCAL_MACHINE, base, L"DeviceState");
        if (state & 0x4 /*NOTPRESENT*/) continue;
        const std::wstring fx = base + L"\\FxProperties";
        const std::wstring efx = readString (HKEY_LOCAL_MACHINE, fx, kEfx);
        std::printf ("%s  %s  state=%lu\n    SFX %s\n    MFX %s\n    EFX %s%s\n", utf8 (ep).c_str(), utf8 (deviceName (ep)).c_str(), state,
                     utf8 (readString (HKEY_LOCAL_MACHINE, fx, kSfx)).c_str(), utf8 (readString (HKEY_LOCAL_MACHINE, fx, kMfx)).c_str(),
                     utf8 (efx).c_str(), _wcsicmp (efx.c_str(), kApoClsid) == 0 ? "  <- Rovno" : "");
    }
    return 0;
}

int install (const std::wstring& ep)
{
    const std::wstring fx = std::wstring (kRender) + L"\\" + ep + L"\\FxProperties";
    const std::wstring backup = std::wstring (kBackup) + L"\\" + ep;
    bool efxExists = false, modesExists = false, sysFxExists = false;
    const std::wstring efx = readString (HKEY_LOCAL_MACHINE, fx, kEfx, &efxExists);
    if (efxExists && _wcsicmp (efx.c_str(), kApoClsid) == 0) { std::printf ("already installed\n"); return 0; }
    const std::wstring modes = readString (HKEY_LOCAL_MACHINE, fx, kEfxModes, &modesExists);
    const DWORD sysFx = readDword (HKEY_LOCAL_MACHINE, fx, kDisableSysFx, &sysFxExists);
    if (efxExists && ! efx.empty())
    {
        // Another endpoint effect is there (a driver's own): keep it, do not replace it.
        std::printf ("the endpoint-effect slot is taken by %s; not touching it\n", utf8 (efx).c_str());
        return 3;
    }
    // Remember what was there, so uninstall puts it back exactly.
    writeString (HKEY_LOCAL_MACHINE, backup, L"efx", efxExists ? efx : kNone);
    writeString (HKEY_LOCAL_MACHINE, backup, L"efxModes", modesExists ? modes : kNone);
    writeString (HKEY_LOCAL_MACHINE, backup, L"disableSysFx", sysFxExists ? std::to_wstring (sysFx) : kNone);
    writeString (HKEY_LOCAL_MACHINE, backup, L"name", deviceName (ep));

    LONG r = writeString (HKEY_LOCAL_MACHINE, fx, kEfx, kApoClsid);
    if (r != ERROR_SUCCESS) { std::printf ("cannot write FxProperties (error %ld); run as administrator\n", r); return 4; }
    if (! modesExists) writeMulti (HKEY_LOCAL_MACHINE, fx, kEfxModes, kDefaultMode);
    if (sysFxExists) deleteValue (HKEY_LOCAL_MACHINE, fx, kDisableSysFx);   // "disable all enhancements" would bypass us
    std::printf ("installed on %s %s\n", utf8 (ep).c_str(), utf8 (deviceName (ep)).c_str());
    return 0;
}

int uninstall (const std::wstring& ep)
{
    const std::wstring fx = std::wstring (kRender) + L"\\" + ep + L"\\FxProperties";
    const std::wstring backup = std::wstring (kBackup) + L"\\" + ep;
    bool have = false;
    const std::wstring efx = readString (HKEY_LOCAL_MACHINE, backup, L"efx", &have);
    if (! have) { std::printf ("no backup for this device; removing only the Rovno value\n"); }
    const std::wstring cur = readString (HKEY_LOCAL_MACHINE, fx, kEfx);
    if (_wcsicmp (cur.c_str(), kApoClsid) == 0)
    {
        if (! have || efx == kNone) deleteValue (HKEY_LOCAL_MACHINE, fx, kEfx);
        else writeString (HKEY_LOCAL_MACHINE, fx, kEfx, efx);
    }
    if (have)
    {
        const std::wstring modes = readString (HKEY_LOCAL_MACHINE, backup, L"efxModes");
        if (modes == kNone) deleteValue (HKEY_LOCAL_MACHINE, fx, kEfxModes);
        const std::wstring sysFx = readString (HKEY_LOCAL_MACHINE, backup, L"disableSysFx");
        if (sysFx != kNone && ! sysFx.empty())
        {
            const DWORD v = (DWORD) std::stoul (sysFx);
            writeValue (HKEY_LOCAL_MACHINE, fx, kDisableSysFx, REG_DWORD, &v, sizeof (v));
        }
        RegDeleteTreeW (HKEY_LOCAL_MACHINE, backup.c_str());
    }
    std::printf ("removed from %s\n", utf8 (ep).c_str());
    return 0;
}

// Writes the endpoint-effect CLSID (or removes it when clsid is empty) through the Sound control
// panel's interface, so the audio service rebuilds the device's pipeline on its next stream.
int policyWrite (const std::wstring& ep, const std::wstring& clsid)
{
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    IPolicyConfig* pc = nullptr;
    HRESULT hr = CoCreateInstance (kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof (IPolicyConfig), (void**) &pc);
    if (FAILED (hr)) { std::printf ("policy config unavailable (0x%08lx)\n", hr); return 6; }
    const std::wstring id = L"{0.0.0.00000000}." + ep;
    PROPVARIANT v;
    PropVariantInit (&v);
    if (! clsid.empty())
    {
        v.vt = VT_LPWSTR;
        v.pwszVal = const_cast<wchar_t*> (clsid.c_str());
    }
    hr = pc->SetPropertyValue (id.c_str(), TRUE, kFxEndpointClsid, &v);
    pc->Release();
    std::printf ("policy write %s: 0x%08lx\n", clsid.empty() ? "(remove)" : utf8 (clsid).c_str(), hr);
    return SUCCEEDED (hr) ? 0 : 7;
}

// Restarts the Windows audio service so it rebuilds every device's processing with the new
// effect settings. All sound stops for about a second; programs carry on by themselves.
int restartAudio()
{
    SC_HANDLE scm = OpenSCManagerW (nullptr, nullptr, SC_MANAGER_CONNECT);
    if (! scm) return 9;
    SC_HANDLE svc = OpenServiceW (scm, L"Audiosrv", SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
    if (! svc) { CloseServiceHandle (scm); return 9; }
    SERVICE_STATUS st {};
    ControlService (svc, SERVICE_CONTROL_STOP, &st);
    for (int i = 0; i < 100 && QueryServiceStatus (svc, &st) && st.dwCurrentState != SERVICE_STOPPED; ++i) Sleep (100);
    const bool started = StartServiceW (svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    for (int i = 0; i < 100 && QueryServiceStatus (svc, &st) && st.dwCurrentState != SERVICE_RUNNING; ++i) Sleep (100);
    CloseServiceHandle (svc);
    CloseServiceHandle (scm);
    std::printf ("audio service %s\n", started && st.dwCurrentState == SERVICE_RUNNING ? "restarted" : "NOT running");
    return started ? 0 : 10;
}

// Prints the device's master volume, and sets it when `value` is 0..1.
int volume (const std::wstring& ep, double value)
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioEndpointVolume* vol = nullptr;
    HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en);
    if (SUCCEEDED (hr)) hr = en->GetDevice ((L"{0.0.0.00000000}." + ep).c_str(), &dev);
    if (SUCCEEDED (hr)) hr = dev->Activate (__uuidof (IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**) &vol);
    if (FAILED (hr)) { std::printf ("cannot open the device volume (0x%08lx)\n", hr); return 8; }
    if (value >= 0 && value <= 1) vol->SetMasterVolumeLevelScalar ((float) value, nullptr);
    float scalar = 0, db = 0;
    vol->GetMasterVolumeLevelScalar (&scalar);
    vol->GetMasterVolumeLevel (&db);
    DWORD hw = 0;
    vol->QueryHardwareSupport (&hw);
    std::printf ("volume %.2f (%.1f dB), hardware volume %s\n", scalar, db, (hw & ENDPOINT_HARDWARE_SUPPORT_VOLUME) ? "yes" : "no");
    vol->Release(); dev->Release(); en->Release();
    return 0;
}

rv::SharedBlock* openShared (const std::wstring& ep, HANDLE& h)
{
    h = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, (L"Global\\Rovno." + ep).c_str());
    if (! h) return nullptr;
    auto* b = static_cast<rv::SharedBlock*> (MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (rv::SharedBlock)));
    if (! b || b->magic != rv::kSharedMagic || b->version != rv::kSharedVersion) { if (b) UnmapViewOfFile (b); CloseHandle (h); return nullptr; }
    return b;
}

int monitor (const std::wstring& ep, int seconds)
{
    HANDLE h;
    rv::SharedBlock* b = openShared (ep, h);
    if (! b) { std::printf ("the effect is not running on this device (no shared block; error %lu)\n", GetLastError()); return 5; }
    for (int i = 0; i < seconds * 2; ++i)
    {
        const auto& t = b->telemetry;
        std::printf ("beat %u  %u Hz x%u  src %u  in M %.1f S %.1f  out S %.1f  leveler %+.1f dB  limiter %.1f dB  peak %.3f  frames %u  enabled %u\n",
                     t.heartbeat, t.sampleRate, t.channels, t.paramsSource, t.inMomentary, t.inShortTerm, t.outShortTerm,
                     t.levelerGainDb, t.limiterMinGainDb, t.outPeak, t.framesWritten, b->params.enabled);
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
    }
    UnmapViewOfFile (b);
    CloseHandle (h);
    return 0;
}

bool saveParams (const std::wstring& ep, const rv::EngineParams& p)
{
    wchar_t dir[MAX_PATH] {};
    GetEnvironmentVariableW (L"ProgramData", dir, MAX_PATH);
    const std::wstring folder = std::wstring (dir) + L"\\Rovno";
    CreateDirectoryW (folder.c_str(), nullptr);
    HANDLE f = CreateFileW ((folder + L"\\" + ep + L".params").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const uint32_t head[3] = { rv::kSharedMagic, rv::kSharedVersion, (uint32_t) sizeof (p) };
    DWORD w;
    const bool ok = WriteFile (f, head, sizeof (head), &w, nullptr) && WriteFile (f, &p, sizeof (p), &w, nullptr);
    CloseHandle (f);
    return ok;
}

int set (const std::wstring& ep, int argc, wchar_t** argv)
{
    HANDLE h;
    rv::SharedBlock* b = openShared (ep, h);
    rv::EngineParams p = b ? b->params : rv::EngineParams {};
    for (int i = 0; i < argc; ++i)
    {
        const std::wstring a = argv[i];
        const auto eq = a.find (L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring k = a.substr (0, eq);
        const float v = (float) _wtof (a.c_str() + eq + 1);
        if (k == L"enabled") p.enabled = (uint32_t) v;
        else if (k == L"loudness") p.loudness = (uint32_t) v;
        else if (k == L"target") p.targetLufs = v;
        else if (k == L"maxboost") p.maxBoostDb = v;
        else if (k == L"output") p.outputDb = v;
        else if (k == L"fid") p.fidelity = (int) v;
        else if (k == L"amb") p.ambience = (int) v;
        else if (k == L"surr") p.surround = (int) v;
        else if (k == L"bass") p.bass = (int) v;
        else if (k == L"boost") p.boost = (int) v;
        else if (k.size() == 3 && k[0] == L'e' && k[1] == L'q') p.eqDb[k[2] - L'0'] = v;
        else std::printf ("unknown key %s\n", utf8 (k).c_str());
    }
    if (b) { rv::writeParams (*b, p); UnmapViewOfFile (b); CloseHandle (h); }
    const bool saved = saveParams (ep, p);
    std::printf ("%s%s\n", b ? "sent to the running effect" : "effect not running", saved ? ", saved" : ", NOT saved");
    return 0;
}
} // namespace

int wmain (int argc, wchar_t** argv)
{
    if (argc < 2) { std::printf ("usage: rovno_ctl list | install <ep> | uninstall <ep> | monitor <ep> [sec] | set <ep> key=value...\n"); return 1; }
    const std::wstring cmd = argv[1];
    if (cmd == L"list") return list();
    if (cmd == L"restart-audio") return restartAudio();
    if (cmd == L"disable-all")
    {
        // Used by the uninstaller: every device back as it was, then the effect is unloaded.
        for (const auto& ep : endpoints())
            if (_wcsicmp (readString (HKEY_LOCAL_MACHINE, std::wstring (kRender) + L"\\" + ep + L"\\FxProperties", kEfx).c_str(), kApoClsid) == 0)
                uninstall (ep);
        return restartAudio();
    }
    if (argc < 3) return 1;
    if (cmd == L"enable") { const int r = install (argv[2]); return r == 0 ? restartAudio() : r; }
    if (cmd == L"disable") { const int r = uninstall (argv[2]); return r == 0 ? restartAudio() : r; }
    const std::wstring ep = argv[2];
    if (cmd == L"install") return install (ep);
    if (cmd == L"uninstall") return uninstall (ep);
    if (cmd == L"monitor") return monitor (ep, argc > 3 ? _wtoi (argv[3]) : 10);
    if (cmd == L"set") return set (ep, argc - 3, argv + 3);
    if (cmd == L"volume") return volume (ep, argc > 3 ? _wtof (argv[3]) : -1.0);
    if (cmd == L"refresh") return policyWrite (ep, readString (HKEY_LOCAL_MACHINE, std::wstring (kRender) + L"\\" + ep + L"\\FxProperties", kEfx));
    return 1;
}
