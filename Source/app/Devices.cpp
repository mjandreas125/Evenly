#include "Devices.h"
#include <windows.h>
#include <initguid.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audiopolicy.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <algorithm>

namespace rv
{
namespace
{
const wchar_t* kRender = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";
const wchar_t* kEfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";
const wchar_t* kApoClsid = L"{F9DCA572-B13F-43A6-B8EE-113925372DDC}";
// PKEY_AudioEndpoint_FormFactor / jack: digital outputs report SPDIF (8) or DigitalAudioDisplayDevice (9).
const PROPERTYKEY kFormFactor = { { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 0 };

// The interface behind the Windows Sound control panel (undocumented, stable since Windows 7).
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
    virtual HRESULT __stdcall GetPropertyValue (PCWSTR, BOOL, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT __stdcall SetPropertyValue (PCWSTR, BOOL, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT __stdcall SetDefaultEndpoint (PCWSTR, ERole) = 0;
    virtual HRESULT __stdcall SetEndpointVisibility (PCWSTR, INT) = 0;
};
const CLSID kPolicyConfigClient = { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };

std::wstring regString (const std::wstring& path, const wchar_t* name)
{
    wchar_t buf[512] {};
    DWORD size = sizeof (buf);
    if (RegGetValueW (HKEY_LOCAL_MACHINE, path.c_str(), name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

std::wstring guidOf (const std::wstring& id)   // "{0.0.0.00000000}.{guid}" -> "{guid}"
{
    const auto dot = id.find (L"}.");
    return dot == std::wstring::npos ? id : id.substr (dot + 2);
}

std::wstring upper (std::wstring s) { for (auto& c : s) c = (wchar_t) towupper (c); return s; }
std::wstring lowerStr (std::wstring s) { for (auto& c : s) c = (wchar_t) towlower (c); return s; }
} // namespace

namespace devices
{
// Ask Windows itself how it spells this endpoint; fall back to the lower-case form it always uses.
std::wstring endpointId (const std::wstring& guid)
{
    std::wstring out;
    IMMDeviceEnumerator* en = nullptr;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))
        && SUCCEEDED (en->EnumAudioEndpoints (eRender, DEVICE_STATEMASK_ALL, &col)))
    {
        UINT n = 0;
        col->GetCount (&n);
        for (UINT i = 0; i < n && out.empty(); ++i)
        {
            IMMDevice* dev = nullptr;
            LPWSTR id = nullptr;
            if (SUCCEEDED (col->Item (i, &dev)) && SUCCEEDED (dev->GetId (&id)))
            {
                if (_wcsicmp (guidOf (id).c_str(), guid.c_str()) == 0) out = id;
                CoTaskMemFree (id);
            }
            if (dev) dev->Release();
        }
        col->Release();
    }
    if (en) en->Release();
    return out.empty() ? L"{0.0.0.00000000}." + lowerStr (guid) : out;
}

std::wstring defaultGuid()
{
    std::wstring out;
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))
        && SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eMultimedia, &dev)))
    {
        LPWSTR id = nullptr;
        if (SUCCEEDED (dev->GetId (&id))) { out = upper (guidOf (id)); CoTaskMemFree (id); }
    }
    if (dev) dev->Release();
    if (en) en->Release();
    return out;
}

std::vector<OutputDevice> list()
{
    std::vector<OutputDevice> out;
    IMMDeviceEnumerator* en = nullptr;
    IMMDeviceCollection* col = nullptr;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))) return out;
    const std::wstring def = defaultGuid();
    // Unplugged too: an HDMI monitor that is switched off is still the device the user wants.
    if (SUCCEEDED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &col)))
    {
        UINT n = 0;
        col->GetCount (&n);
        for (UINT i = 0; i < n; ++i)
        {
            IMMDevice* dev = nullptr;
            IPropertyStore* props = nullptr;
            LPWSTR id = nullptr;
            if (FAILED (col->Item (i, &dev)) || FAILED (dev->GetId (&id))) { if (dev) dev->Release(); continue; }
            OutputDevice d;
            d.id = id;                      // exactly as Windows spells it: WASAPI compares the string
            d.guid = upper (guidOf (id));   // upper case for the registry keys and the settings file
            CoTaskMemFree (id);
            if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &props)))
            {
                PROPVARIANT v;
                PropVariantInit (&v);
                if (SUCCEEDED (props->GetValue (PKEY_Device_DeviceDesc, &v)) && v.vt == VT_LPWSTR) d.name = v.pwszVal;
                PropVariantClear (&v);
                if (SUCCEEDED (props->GetValue (PKEY_DeviceInterface_FriendlyName, &v)) && v.vt == VT_LPWSTR) d.adapter = v.pwszVal;
                PropVariantClear (&v);
                if (SUCCEEDED (props->GetValue (kFormFactor, &v)) && v.vt == VT_UI4) d.digital = v.ulVal == 8 || v.ulVal == 9;
                PropVariantClear (&v);
                props->Release();
            }
            const std::wstring fx = std::wstring (kRender) + L"\\" + d.guid + L"\\FxProperties";
            const std::wstring efx = regString (fx, kEfx);
            d.rovnoInstalled = _wcsicmp (efx.c_str(), kApoClsid) == 0;
            d.slotTaken = ! efx.empty() && ! d.rovnoInstalled;
            d.isFxSound = d.adapter.find (L"DFX") != std::wstring::npos || d.adapter.find (L"FxSound") != std::wstring::npos;
            for (const wchar_t* v : { L"Virtual", L"virtual", L"VB-Audio", L"CABLE", L"Voicemeeter", L"Sonar", L"Steam Streaming", L"DFX", L"FxSound" })
                if (d.adapter.find (v) != std::wstring::npos || d.name.find (v) != std::wstring::npos) d.isVirtual = true;
            DWORD st = 0;
            dev->GetState (&st);
            d.plugged = (st & DEVICE_STATE_ACTIVE) != 0;
            d.isDefault = d.guid == def;
            out.push_back (d);
            dev->Release();
        }
        col->Release();
    }
    en->Release();
    return out;
}

bool setDefault (const std::wstring& guid)
{
    IPolicyConfig* pc = nullptr;
    if (FAILED (CoCreateInstance (kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof (IPolicyConfig), (void**) &pc))) return false;
    const std::wstring id = endpointId (guid);
    bool ok = true;
    for (ERole r : { eConsole, eMultimedia, eCommunications }) ok &= SUCCEEDED (pc->SetDefaultEndpoint (id.c_str(), r));
    pc->Release();
    return ok;
}

std::wstring helperPath()
{
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW (nullptr, path, MAX_PATH);
    std::wstring p = path;
    return p.substr (0, p.find_last_of (L"\\/") + 1) + L"rovno_ctl.exe";
}

bool installElevated (const std::wstring& guid, bool install, std::wstring& error)
{
    const std::wstring exe = helperPath(), args = std::wstring (install ? L"enable " : L"disable ") + guid;
    SHELLEXECUTEINFOW sei { sizeof (sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (! ShellExecuteExW (&sei))
    {
        error = GetLastError() == ERROR_CANCELLED ? L"cancelled" : L"cannot start rovno_ctl";
        return false;
    }
    WaitForSingleObject (sei.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess (sei.hProcess, &code);
    CloseHandle (sei.hProcess);
    if (code != 0) error = L"rovno_ctl exit " + std::to_wstring (code);
    return code == 0;
}

std::wstring deviceUsedBy (const wchar_t* exe)
{
    std::wstring found;
    IMMDeviceEnumerator* en = nullptr;
    IMMDeviceCollection* col = nullptr;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))) return found;
    if (SUCCEEDED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &col)))
    {
        UINT n = 0;
        col->GetCount (&n);
        for (UINT i = 0; i < n && found.empty(); ++i)
        {
            IMMDevice* dev = nullptr;
            IAudioSessionManager2* mgr = nullptr;
            IAudioSessionEnumerator* list = nullptr;
            if (SUCCEEDED (col->Item (i, &dev)) && SUCCEEDED (dev->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**) &mgr))
                && SUCCEEDED (mgr->GetSessionEnumerator (&list)))
            {
                int count = 0;
                list->GetCount (&count);
                for (int s = 0; s < count && found.empty(); ++s)
                {
                    IAudioSessionControl* c = nullptr;
                    IAudioSessionControl2* c2 = nullptr;
                    DWORD pid = 0;
                    if (SUCCEEDED (list->GetSession (s, &c)) && SUCCEEDED (c->QueryInterface (__uuidof (IAudioSessionControl2), (void**) &c2))
                        && SUCCEEDED (c2->GetProcessId (&pid)) && pid != 0)
                        if (HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
                        {
                            wchar_t path[MAX_PATH];
                            DWORD len = MAX_PATH;
                            if (QueryFullProcessImageNameW (h, 0, path, &len))
                            {
                                const wchar_t* name = wcsrchr (path, L'\\');
                                LPWSTR id = nullptr;
                                if (_wcsicmp (name ? name + 1 : path, exe) == 0 && SUCCEEDED (dev->GetId (&id)))
                                {
                                    found = upper (guidOf (id));
                                    CoTaskMemFree (id);
                                }
                            }
                            CloseHandle (h);
                        }
                    if (c2) c2->Release();
                    if (c) c->Release();
                }
            }
            if (list) list->Release();
            if (mgr) mgr->Release();
            if (dev) dev->Release();
        }
        col->Release();
    }
    en->Release();
    return found;
}

bool fxSoundRunning()
{
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe { sizeof (pe) };
    bool found = false;
    for (BOOL ok = Process32FirstW (snap, &pe); ok && ! found; ok = Process32NextW (snap, &pe))
        found = _wcsicmp (pe.szExeFile, L"dfx.exe") == 0 || _wcsicmp (pe.szExeFile, L"FxSound.exe") == 0;
    CloseHandle (snap);
    return found;
}
} // namespace devices
} // namespace rv
