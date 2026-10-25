// COM plumbing of the Rovno effect DLL: class factory and (un)registration.
#include "RovnoApo.h"
#include <string>

static HINSTANCE module = nullptr;
static long locks = 0;

class ClassFactory : public IClassFactory
{
public:
    HRESULT __stdcall QueryInterface (const IID& iid, void** ppv) override
    {
        if (! ppv) return E_POINTER;
        if (iid == __uuidof (IUnknown) || iid == __uuidof (IClassFactory)) { *ppv = static_cast<IClassFactory*> (this); AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return (ULONG) InterlockedIncrement (&refs); }
    ULONG __stdcall Release() override
    {
        const long r = InterlockedDecrement (&refs);
        if (r == 0) delete this;
        return (ULONG) r;
    }
    HRESULT __stdcall CreateInstance (IUnknown* outer, const IID& iid, void** ppv) override
    {
        if (! ppv) return E_POINTER;
        if (outer && iid != __uuidof (IUnknown)) return E_NOINTERFACE;
        auto* apo = new (std::nothrow) RovnoApo (outer);
        if (! apo) return E_OUTOFMEMORY;
        const HRESULT hr = apo->NonDelegatingQueryInterface (iid, ppv);
        apo->NonDelegatingRelease();
        return hr;
    }
    HRESULT __stdcall LockServer (BOOL lock) override
    {
        if (lock) InterlockedIncrement (&locks); else InterlockedDecrement (&locks);
        return S_OK;
    }
private:
    long refs = 1;
};

BOOL WINAPI DllMain (HINSTANCE h, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) { module = h; DisableThreadLibraryCalls (h); }
    return TRUE;
}

STDAPI DllCanUnloadNow() { return RovnoApo::instances == 0 && locks == 0 ? S_OK : S_FALSE; }

STDAPI DllGetClassObject (REFCLSID clsid, REFIID iid, void** ppv)
{
    if (clsid != kRovnoApoClsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto* f = new (std::nothrow) ClassFactory();
    if (! f) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface (iid, ppv);
    f->Release();
    return hr;
}

static std::wstring clsidString()
{
    wchar_t buf[64] {};
    StringFromGUID2 (kRovnoApoClsid, buf, 64);
    return buf;
}

static bool setString (HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value)
{
    HKEY k;
    if (RegCreateKeyExW (root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    const LONG r = RegSetValueExW (k, name, 0, REG_SZ, reinterpret_cast<const BYTE*> (value.c_str()), (DWORD) ((value.size() + 1) * sizeof (wchar_t)));
    RegCloseKey (k);
    return r == ERROR_SUCCESS;
}

static bool setDword (HKEY root, const std::wstring& path, const wchar_t* name, DWORD value)
{
    HKEY k;
    if (RegCreateKeyExW (root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    const LONG r = RegSetValueExW (k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*> (&value), sizeof (value));
    RegCloseKey (k);
    return r == ERROR_SUCCESS;
}

static std::wstring guidString (const GUID& g)
{
    wchar_t buf[64] {};
    StringFromGUID2 (g, buf, 64);
    return buf;
}

// What RegisterAPO() from the SDK library writes: HKCR\AudioEngine\AudioProcessingObjects\{clsid}.
static bool registerApo()
{
    const APO_REG_PROPERTIES p = RovnoApo::registration();
    const std::wstring key = L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\" + clsidString();
    return setString (HKEY_LOCAL_MACHINE, key, L"FriendlyName", p.szFriendlyName)
        && setString (HKEY_LOCAL_MACHINE, key, L"Copyright", p.szCopyrightInfo)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MajorVersion", p.u32MajorVersion)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MinorVersion", p.u32MinorVersion)
        && setDword (HKEY_LOCAL_MACHINE, key, L"Flags", (DWORD) p.Flags)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MinInputConnections", p.u32MinInputConnections)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MaxInputConnections", p.u32MaxInputConnections)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MinOutputConnections", p.u32MinOutputConnections)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MaxOutputConnections", p.u32MaxOutputConnections)
        && setDword (HKEY_LOCAL_MACHINE, key, L"MaxInstances", p.u32MaxInstances)
        && setDword (HKEY_LOCAL_MACHINE, key, L"NumAPOInterfaces", p.u32NumAPOInterfaces)
        && setString (HKEY_LOCAL_MACHINE, key, L"APOInterface0", guidString (p.iidAPOInterfaceList[0]));
}

STDAPI DllUnregisterServer();

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW (module, path, MAX_PATH);
    const std::wstring key = L"SOFTWARE\\Classes\\CLSID\\" + clsidString();
    if (! registerApo()
        || ! setString (HKEY_LOCAL_MACHINE, key, nullptr, L"Rovno sound effect")
        || ! setString (HKEY_LOCAL_MACHINE, key + L"\\InprocServer32", nullptr, path)
        || ! setString (HKEY_LOCAL_MACHINE, key + L"\\InprocServer32", L"ThreadingModel", L"Both"))
    {
        DllUnregisterServer();
        return E_FAIL;
    }
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    RegDeleteTreeW (HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + clsidString()).c_str());
    RegDeleteTreeW (HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\" + clsidString()).c_str());
    return S_OK;
}
