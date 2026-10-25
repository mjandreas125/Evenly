#include "RovnoApo.h"
#include <immintrin.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <cstdarg>
#include <cwchar>
#include <new>
#include <sddl.h>

long RovnoApo::instances = 0;

namespace
{
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
constexpr GUID kFloatFormat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
// PKEY_AudioEndpoint_GUID
const PROPERTYKEY kEndpointGuidKey = { { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 4 };

// A minimal IAudioMediaType for proposing the format we work in (float32, same rate/channels).
class MediaType : public IAudioMediaType
{
public:
    explicit MediaType (const UNCOMPRESSEDAUDIOFORMAT& f) : fmt (f)
    {
        std::memset (&wfx, 0, sizeof (wfx));
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels = (WORD) f.dwSamplesPerFrame;
        wfx.Format.nSamplesPerSec = (DWORD) f.fFramesPerSecond;
        wfx.Format.wBitsPerSample = (WORD) (f.dwBytesPerSampleContainer * 8);
        wfx.Format.nBlockAlign = (WORD) (f.dwBytesPerSampleContainer * f.dwSamplesPerFrame);
        wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
        wfx.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
        wfx.Samples.wValidBitsPerSample = (WORD) f.dwValidBitsPerSample;
        wfx.dwChannelMask = f.dwChannelMask;
        wfx.SubFormat = f.guidFormatType;
    }
    HRESULT __stdcall QueryInterface (const IID& iid, void** ppv) override
    {
        if (! ppv) return E_POINTER;
        if (iid == __uuidof (IUnknown) || iid == __uuidof (IAudioMediaType)) { *ppv = static_cast<IAudioMediaType*> (this); AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return (ULONG) InterlockedIncrement (&refs); }
    ULONG __stdcall Release() override { const long r = InterlockedDecrement (&refs); if (r == 0) delete this; return (ULONG) r; }
    HRESULT __stdcall IsCompressedFormat (BOOL* c) override { if (! c) return E_POINTER; *c = FALSE; return S_OK; }
    HRESULT __stdcall IsEqual (IAudioMediaType* other, DWORD* flags) override
    {
        if (! other || ! flags) return E_POINTER;
        *flags = 0;
        UNCOMPRESSEDAUDIOFORMAT o {};
        if (FAILED (other->GetUncompressedAudioFormat (&o))) return S_OK;
        if (o.guidFormatType == fmt.guidFormatType) *flags |= AUDIOMEDIATYPE_EQUAL_FORMAT_TYPES;
        if (std::memcmp (&o, &fmt, sizeof (o)) == 0) *flags |= AUDIOMEDIATYPE_EQUAL_FORMAT_DATA | AUDIOMEDIATYPE_EQUAL_FORMAT_USER_DATA;
        return S_OK;
    }
    const WAVEFORMATEX* __stdcall GetAudioFormat() override { return &wfx.Format; }
    HRESULT __stdcall GetUncompressedAudioFormat (UNCOMPRESSEDAUDIOFORMAT* f) override { if (! f) return E_POINTER; *f = fmt; return S_OK; }
private:
    long refs = 1;
    UNCOMPRESSEDAUDIOFORMAT fmt;
    WAVEFORMATEXTENSIBLE wfx;
};

// A short log of what the effect did (never from the audio thread): C:\ProgramData\Rovno\effect.log
void logLine (const std::wstring& endpoint, const wchar_t* fmt, ...)
{
    wchar_t dir[MAX_PATH] {};
    if (! GetEnvironmentVariableW (L"ProgramData", dir, MAX_PATH)) wcscpy_s (dir, L"C:\\ProgramData");
    const std::wstring path = std::wstring (dir) + L"\\Rovno\\effect.log";
    HANDLE f = CreateFileW (path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size {};
    if (GetFileSizeEx (f, &size) && size.QuadPart > 512 * 1024)
    {
        CloseHandle (f);
        f = CreateFileW (path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
    }
    SYSTEMTIME t;
    GetLocalTime (&t);
    wchar_t msg[512];
    va_list va;
    va_start (va, fmt);
    _vsnwprintf_s (msg, _TRUNCATE, fmt, va);
    va_end (va);
    wchar_t line[768];
    swprintf_s (line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid %lu %s  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds, GetCurrentProcessId(), endpoint.c_str(), msg);
    char utf8[2048];
    const int n = WideCharToMultiByte (CP_UTF8, 0, line, -1, utf8, sizeof (utf8), nullptr, nullptr);
    DWORD w;
    if (n > 1) WriteFile (f, utf8, (DWORD) (n - 1), &w, nullptr);
    CloseHandle (f);
}

bool isFloat32 (const UNCOMPRESSEDAUDIOFORMAT& f)
{
    return f.guidFormatType == kFloatFormat && f.dwBytesPerSampleContainer == 4 && f.dwValidBitsPerSample == 32 && f.dwSamplesPerFrame >= 1;
}

// Runs the engine; a crash inside it must never take the whole Windows audio down with it.
bool safeProcess (rv::Engine* e, float* x, UINT32 frames, rv::Telemetry* tel)
{
    __try
    {
        e->process (x, (int) frames, tel);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

APO_REG_PROPERTIES RovnoApo::registration()
{
    APO_REG_PROPERTIES p {};
    p.clsid = kRovnoApoClsid;
    p.Flags = kRovnoApoFlags;
    wcscpy_s (p.szFriendlyName, L"Rovno");
    wcscpy_s (p.szCopyrightInfo, L"Rovno contributors, AGPL-3.0");
    p.u32MajorVersion = 1;
    p.u32MinorVersion = 0;
    p.u32MinInputConnections = p.u32MaxInputConnections = 1;
    p.u32MinOutputConnections = p.u32MaxOutputConnections = 1;
    p.u32MaxInstances = 0xffffffff;
    p.u32NumAPOInterfaces = 1;
    p.iidAPOInterfaceList[0] = __uuidof (IAudioProcessingObject);
    return p;
}

RovnoApo::RovnoApo (IUnknown* outerUnknown)
{
    outer = outerUnknown != nullptr ? outerUnknown : reinterpret_cast<IUnknown*> (static_cast<INonDelegatingUnknown*> (this));
    params.enabled = 0;   // pass-through until the app has said what to do
    InterlockedIncrement (&instances);
}

RovnoApo::~RovnoApo()
{
    delete engine;
    if (shared) UnmapViewOfFile (shared);
    if (mapping) CloseHandle (mapping);
    InterlockedDecrement (&instances);
}

HRESULT RovnoApo::QueryInterface (const IID& iid, void** ppv) { return outer->QueryInterface (iid, ppv); }
ULONG RovnoApo::AddRef() { return outer->AddRef(); }
ULONG RovnoApo::Release() { return outer->Release(); }

HRESULT RovnoApo::Reset()
{
    if (engine && ! locked) engine->prepare (frameRate, (int) channels);
    return S_OK;
}

HRESULT RovnoApo::GetLatency (HNSTIME* pTime)
{
    if (! pTime) return E_POINTER;
    *pTime = engine && frameRate > 0 ? (HNSTIME) (engine->latencySamples() * 1.0e7 / frameRate) : 0;
    return S_OK;
}

HRESULT RovnoApo::GetRegistrationProperties (APO_REG_PROPERTIES** pp)
{
    if (! pp) return E_POINTER;
    *pp = static_cast<APO_REG_PROPERTIES*> (CoTaskMemAlloc (sizeof (APO_REG_PROPERTIES)));
    if (! *pp) return E_OUTOFMEMORY;
    **pp = registration();
    return S_OK;
}

HRESULT RovnoApo::GetInputChannelCount (UINT32* n)
{
    if (! n) return E_POINTER;
    *n = channels;
    return S_OK;
}

HRESULT RovnoApo::Initialize (UINT32 cbDataSize, BYTE* pbyData)
{
    if (initialized) return APOERR_ALREADY_INITIALIZED;
    if (! pbyData || cbDataSize < sizeof (APOInitSystemEffects)) return E_INVALIDARG;
    auto* init = reinterpret_cast<APOInitSystemEffects*> (pbyData);
    endpoint.clear();
    if (init->pAPOEndpointProperties)
    {
        PROPVARIANT v;
        PropVariantInit (&v);
        if (SUCCEEDED (init->pAPOEndpointProperties->GetValue (kEndpointGuidKey, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
            endpoint = v.pwszVal;
        PropVariantClear (&v);
    }
    loadSavedParams();
    openShared();
    initialized = true;
    logLine (endpoint, L"Initialize: settings from %s, shared block %s",
             paramsSource == 2 ? L"app" : (paramsSource == 1 ? L"saved file" : L"defaults (pass-through)"), shared ? L"ok" : L"FAILED");
    return S_OK;
}

// We work in float32 with the same rate and channel count on both sides (in place).
HRESULT RovnoApo::checkFormat (IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported)
{
    if (! requested || ! supported) return E_POINTER;
    *supported = nullptr;
    UNCOMPRESSEDAUDIOFORMAT req {};
    if (FAILED (requested->GetUncompressedAudioFormat (&req))) return APOERR_FORMAT_NOT_SUPPORTED;
    UNCOMPRESSEDAUDIOFORMAT opp = req;
    if (opposite && FAILED (opposite->GetUncompressedAudioFormat (&opp))) opp = req;
    const bool ok = isFloat32 (req) && req.dwSamplesPerFrame == opp.dwSamplesPerFrame && req.fFramesPerSecond == opp.fFramesPerSecond;
    if (ok)
    {
        requested->AddRef();
        *supported = requested;
        return S_OK;
    }
    UNCOMPRESSEDAUDIOFORMAT prop = opp;
    prop.guidFormatType = kFloatFormat;
    prop.dwBytesPerSampleContainer = 4;
    prop.dwValidBitsPerSample = 32;
    *supported = new (std::nothrow) MediaType (prop);
    return *supported ? S_FALSE : E_OUTOFMEMORY;
}

HRESULT RovnoApo::IsInputFormatSupported (IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported)
{
    return checkFormat (opposite, requested, supported);
}

HRESULT RovnoApo::IsOutputFormatSupported (IAudioMediaType* opposite, IAudioMediaType* requested, IAudioMediaType** supported)
{
    return checkFormat (opposite, requested, supported);
}

// Settings the app saved last time: C:\ProgramData\Rovno\<endpoint>.params
void RovnoApo::loadSavedParams()
{
    if (endpoint.empty()) return;
    wchar_t dir[MAX_PATH] {};
    if (! GetEnvironmentVariableW (L"ProgramData", dir, MAX_PATH)) wcscpy_s (dir, L"C:\\ProgramData");
    const std::wstring path = std::wstring (dir) + L"\\Rovno\\" + endpoint + L".params";
    HANDLE f = CreateFileW (path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    struct { uint32_t magic, version, size; } head {};
    DWORD got = 0;
    rv::EngineParams p {};
    if (ReadFile (f, &head, sizeof (head), &got, nullptr) && got == sizeof (head) && head.magic == rv::kSharedMagic
        && head.version == rv::kSharedVersion && head.size == sizeof (p)
        && ReadFile (f, &p, sizeof (p), &got, nullptr) && got == sizeof (p))
    {
        params = p;
        paramsSource = 1;
    }
    CloseHandle (f);
}

// The block shared with the app: Global\Rovno.<endpoint>. Readable and writable by the user's app
// (medium integrity) although audiodg creates it.
void RovnoApo::openShared()
{
    if (endpoint.empty()) return;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (! ConvertStringSecurityDescriptorToSecurityDescriptorW (L"D:(A;;GA;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)S:(ML;;NW;;;LW)",
                                                               SDDL_REVISION_1, &sd, nullptr))
        sd = nullptr;
    SECURITY_ATTRIBUTES sa { sizeof (sa), sd, FALSE };
    const std::wstring name = L"Global\\Rovno." + endpoint;
    mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE, 0, sizeof (rv::SharedBlock), name.c_str());
    const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    if (sd) LocalFree (sd);
    if (! mapping) return;
    shared = static_cast<rv::SharedBlock*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (rv::SharedBlock)));
    if (! shared) { CloseHandle (mapping); mapping = nullptr; return; }
    if (! existed || shared->magic != rv::kSharedMagic || shared->version != rv::kSharedVersion)
    {
        std::memset (shared, 0, sizeof (rv::SharedBlock));
        shared->magic = rv::kSharedMagic;
        shared->version = rv::kSharedVersion;
        shared->params = params;   // let the app see what is running
    }
    else if (shared->paramsWritten)
    {
        rv::EngineParams p;
        if (rv::tryReadParams (*shared, p)) { params = p; paramsSource = 2; }
    }
}

HRESULT RovnoApo::LockForProcess (UINT32 nIn, APO_CONNECTION_DESCRIPTOR** in, UINT32 nOut, APO_CONNECTION_DESCRIPTOR** out)
{
    if (! initialized) return APOERR_NOT_INITIALIZED;
    if (locked) return APOERR_APO_LOCKED;
    if (nIn != 1 || nOut != 1 || ! in || ! out || ! in[0] || ! out[0] || ! in[0]->pFormat || ! out[0]->pFormat) return APOERR_NUM_CONNECTIONS_INVALID;
    UNCOMPRESSEDAUDIOFORMAT fi {}, fo {};
    if (FAILED (in[0]->pFormat->GetUncompressedAudioFormat (&fi)) || FAILED (out[0]->pFormat->GetUncompressedAudioFormat (&fo)))
        return APOERR_FORMAT_NOT_SUPPORTED;
    if (! isFloat32 (fi) || ! isFloat32 (fo) || fi.dwSamplesPerFrame != fo.dwSamplesPerFrame || fi.fFramesPerSecond != fo.fFramesPerSecond)
    {
        logLine (endpoint, L"LockForProcess: unsupported format %lu ch %.0f Hz %lu bit", fi.dwSamplesPerFrame, fi.fFramesPerSecond, fi.dwValidBitsPerSample);
        return APOERR_FORMAT_NOT_SUPPORTED;
    }
    channels = fi.dwSamplesPerFrame;
    frameRate = fi.fFramesPerSecond;
    if (shared && shared->paramsWritten)
    {
        rv::EngineParams p;
        if (rv::tryReadParams (*shared, p)) { params = p; paramsSource = 2; }
    }
    delete engine;
    engine = new (std::nothrow) rv::Engine();
    if (engine)
    {
        engine->prepare (frameRate, (int) channels);
        engine->setParams (params);
    }
    locked = true;
    logLine (endpoint, L"LockForProcess: %lu ch, %.0f Hz, up to %u frames, processing %s", channels, frameRate,
             in[0]->u32MaxFrameCount, engine ? (params.enabled ? L"on" : L"off (pass-through)") : L"NO ENGINE");
    return S_OK;
}

HRESULT RovnoApo::UnlockForProcess()
{
    if (! locked) return APOERR_ALREADY_UNLOCKED;
    locked = false;
    return S_OK;
}

#pragma code_seg (push, "RT_CODE")
void RovnoApo::APOProcess (UINT32, APO_CONNECTION_PROPERTY** in, UINT32, APO_CONNECTION_PROPERTY** out)
{
    APO_CONNECTION_PROPERTY* ic = in[0];
    APO_CONNECTION_PROPERTY* oc = out[0];
    const UINT32 frames = ic->u32ValidFrameCount;
    float* src = reinterpret_cast<float*> (ic->pBuffer);
    float* dst = reinterpret_cast<float*> (oc->pBuffer);
    const size_t count = (size_t) frames * channels;
    rv::Telemetry* tel = shared ? &shared->telemetry : &localTelemetry;

    if (ic->u32BufferFlags == BUFFER_SILENT)
    {
        std::memset (dst, 0, count * sizeof (float));
        oc->u32BufferFlags = BUFFER_SILENT;
    }
    else
    {
        if (src != dst) std::memcpy (dst, src, count * sizeof (float));
        if (shared && shared->paramsWritten)
        {
            rv::EngineParams p;
            if (rv::tryReadParams (*shared, p)) { params = p; paramsSource = 2; }
        }
        if (engine)
        {
            const unsigned csr = _mm_getcsr();
            _mm_setcsr (csr | 0x8040);   // flush denormals to zero while we run
            engine->setParams (params);
            if (! safeProcess (engine, dst, frames, tel))
            {
                // Something went badly wrong: stop processing for good, pass the sound through.
                engine = nullptr;
                if (src != dst) std::memcpy (dst, src, count * sizeof (float));
            }
            _mm_setcsr (csr);
        }
        oc->u32BufferFlags = BUFFER_VALID;
    }
    oc->u32ValidFrameCount = frames;
    tel->engineBuild = rv::kSharedVersion;
    tel->paramsSource = paramsSource;
}
#pragma code_seg (pop)

HRESULT RovnoApo::NonDelegatingQueryInterface (const IID& iid, void** ppv)
{
    if (! ppv) return E_POINTER;
    if (iid == __uuidof (IUnknown)) *ppv = static_cast<INonDelegatingUnknown*> (this);
    else if (iid == __uuidof (IAudioProcessingObject)) *ppv = static_cast<IAudioProcessingObject*> (this);
    else if (iid == __uuidof (IAudioProcessingObjectRT)) *ppv = static_cast<IAudioProcessingObjectRT*> (this);
    else if (iid == __uuidof (IAudioProcessingObjectConfiguration)) *ppv = static_cast<IAudioProcessingObjectConfiguration*> (this);
    else if (iid == __uuidof (IAudioSystemEffects)) *ppv = static_cast<IAudioSystemEffects*> (this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    reinterpret_cast<IUnknown*> (*ppv)->AddRef();
    return S_OK;
}

ULONG RovnoApo::NonDelegatingAddRef() { return (ULONG) InterlockedIncrement (&refCount); }

ULONG RovnoApo::NonDelegatingRelease()
{
    const long r = InterlockedDecrement (&refCount);
    if (r == 0) delete this;
    return (ULONG) r;
}
