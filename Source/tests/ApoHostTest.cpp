// Loads RovnoApo.dll the way the Windows audio engine does and drives it: COM creation,
// Initialize with endpoint properties, format negotiation, LockForProcess, APOProcess on a WAV,
// settings through the shared memory, silent buffers, unlock/relock. Compares the output with
// the offline engine. Run elevated (creating Global\ objects needs it, as audiodg has it).
//   apo_host_test <RovnoApo.dll> <in.wav> <expected.wav from rovno_render with the same settings>
#include "../apo/RovnoApo.h"
#include "../tools/Wav.h"
#include <cstdio>
#include <mmreg.h>

static int failures = 0;
#define CHECK(c, ...) do { if (c) std::printf ("ok    " __VA_ARGS__); else { ++failures; std::printf ("FAIL  " __VA_ARGS__); } std::printf ("\n"); } while (0)

static const wchar_t* kTestEndpoint = L"{00000000-0000-0000-0000-0000524F564E}";
static const GUID kFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID kPcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const PROPERTYKEY kEndpointGuidKey = { { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 4 };

struct Props : IPropertyStore
{
    long refs = 1;
    HRESULT __stdcall QueryInterface (const IID& iid, void** ppv) override
    {
        if (iid == __uuidof (IUnknown) || iid == __uuidof (IPropertyStore)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return (ULONG) ++refs; }
    ULONG __stdcall Release() override { return (ULONG) --refs; }
    HRESULT __stdcall GetCount (DWORD* n) override { *n = 1; return S_OK; }
    HRESULT __stdcall GetAt (DWORD, PROPERTYKEY* k) override { *k = kEndpointGuidKey; return S_OK; }
    HRESULT __stdcall GetValue (REFPROPERTYKEY key, PROPVARIANT* v) override
    {
        PropVariantInit (v);
        if (key.fmtid != kEndpointGuidKey.fmtid || key.pid != kEndpointGuidKey.pid) return S_OK;
        const size_t bytes = (wcslen (kTestEndpoint) + 1) * sizeof (wchar_t);
        v->vt = VT_LPWSTR;
        v->pwszVal = static_cast<wchar_t*> (CoTaskMemAlloc (bytes));
        std::memcpy (v->pwszVal, kTestEndpoint, bytes);
        return S_OK;
    }
    HRESULT __stdcall SetValue (REFPROPERTYKEY, REFPROPVARIANT) override { return E_NOTIMPL; }
    HRESULT __stdcall Commit() override { return S_OK; }
};

struct Type : IAudioMediaType
{
    long refs = 1;
    UNCOMPRESSEDAUDIOFORMAT f {};
    WAVEFORMATEX wfx {};
    Type (const GUID& g, DWORD ch, DWORD bytes, float rate)
    {
        f = { g, ch, bytes, bytes * 8, rate, ch == 2 ? 3u : 0u };
        wfx = { (WORD) (g == kFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM), (WORD) ch, (DWORD) rate, (DWORD) rate * ch * bytes, (WORD) (ch * bytes), (WORD) (bytes * 8), 0 };
    }
    HRESULT __stdcall QueryInterface (const IID& iid, void** ppv) override
    {
        if (iid == __uuidof (IUnknown) || iid == __uuidof (IAudioMediaType)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return (ULONG) ++refs; }
    ULONG __stdcall Release() override { return (ULONG) --refs; }
    HRESULT __stdcall IsCompressedFormat (BOOL* c) override { *c = FALSE; return S_OK; }
    HRESULT __stdcall IsEqual (IAudioMediaType*, DWORD* fl) override { *fl = 0; return S_OK; }
    const WAVEFORMATEX* __stdcall GetAudioFormat() override { return &wfx; }
    HRESULT __stdcall GetUncompressedAudioFormat (UNCOMPRESSEDAUDIOFORMAT* o) override { *o = f; return S_OK; }
};

int wmain (int argc, wchar_t** argv)
{
    if (argc < 4) { std::printf ("usage: apo_host_test RovnoApo.dll in.wav expected.wav\n"); return 1; }
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    HMODULE dll = LoadLibraryW (argv[1]);
    CHECK (dll != nullptr, "DLL loads");
    if (! dll) return 1;
    using GetClassObject = HRESULT (__stdcall*) (REFCLSID, REFIID, void**);
    auto gco = reinterpret_cast<GetClassObject> (GetProcAddress (dll, "DllGetClassObject"));
    IClassFactory* factory = nullptr;
    CHECK (gco && SUCCEEDED (gco (kRovnoApoClsid, __uuidof (IClassFactory), (void**) &factory)), "class factory");
    IAudioProcessingObject* apo = nullptr;
    CHECK (factory && SUCCEEDED (factory->CreateInstance (nullptr, __uuidof (IAudioProcessingObject), (void**) &apo)), "instance created");
    if (! apo) return 1;
    IAudioProcessingObjectRT* rt = nullptr;
    IAudioProcessingObjectConfiguration* cfg = nullptr;
    IAudioSystemEffects* sfx = nullptr;
    apo->QueryInterface (__uuidof (IAudioProcessingObjectRT), (void**) &rt);
    apo->QueryInterface (__uuidof (IAudioProcessingObjectConfiguration), (void**) &cfg);
    apo->QueryInterface (__uuidof (IAudioSystemEffects), (void**) &sfx);
    CHECK (rt && cfg && sfx, "RT, configuration and system-effects interfaces");

    APO_REG_PROPERTIES* reg = nullptr;
    CHECK (SUCCEEDED (apo->GetRegistrationProperties (&reg)) && reg && reg->clsid == kRovnoApoClsid, "registration properties");
    CoTaskMemFree (reg);

    // Initialize like audiodg: endpoint properties carry the endpoint GUID.
    Props props;
    APOInitSystemEffects init {};
    init.APOInit.cbSize = sizeof (init);
    init.APOInit.clsid = kRovnoApoClsid;
    init.pAPOEndpointProperties = &props;
    CHECK (SUCCEEDED (apo->Initialize (sizeof (init), reinterpret_cast<BYTE*> (&init))), "Initialize");
    CHECK (apo->Initialize (sizeof (init), reinterpret_cast<BYTE*> (&init)) == APOERR_ALREADY_INITIALIZED, "second Initialize refused");

    rv::wav::Audio a, expected;
    rv::wav::read ([&] { char s[MAX_PATH]; WideCharToMultiByte (CP_ACP, 0, argv[2], -1, s, MAX_PATH, nullptr, nullptr); return std::string (s); }(), a);
    rv::wav::read ([&] { char s[MAX_PATH]; WideCharToMultiByte (CP_ACP, 0, argv[3], -1, s, MAX_PATH, nullptr, nullptr); return std::string (s); }(), expected);
    CHECK (a.channels == 2 && a.rate > 0 && expected.data.size() == a.data.size(), "test audio %d Hz x%d", a.rate, a.channels);

    // Format negotiation.
    Type flt (kFloat, 2, 4, (float) a.rate), pcm (kPcm, 2, 2, (float) a.rate);
    IAudioMediaType* got = nullptr;
    CHECK (apo->IsInputFormatSupported (&flt, &flt, &got) == S_OK && got == &flt, "float32 accepted");
    if (got) got->Release();
    got = nullptr;
    HRESULT hr = apo->IsInputFormatSupported (&flt, &pcm, &got);
    UNCOMPRESSEDAUDIOFORMAT prop {};
    CHECK (hr == S_FALSE && got && SUCCEEDED (got->GetUncompressedAudioFormat (&prop)) && prop.guidFormatType == kFloat
           && prop.dwSamplesPerFrame == 2 && prop.fFramesPerSecond == (float) a.rate, "PCM 16 refused, float32 proposed");
    if (got) got->Release();

    // The shared block exists now (as it does in audiodg); send the settings like the app does.
    HANDLE h = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, (std::wstring (L"Global\\Rovno.") + kTestEndpoint).c_str());
    auto* shared = h ? static_cast<rv::SharedBlock*> (MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (rv::SharedBlock))) : nullptr;
    CHECK (shared && shared->magic == rv::kSharedMagic, "shared block Global\\Rovno.<endpoint> created");
    if (! shared) return 1;
    CHECK (shared->params.enabled == 0, "pass-through until the app sends settings");
    rv::EngineParams p;
    p.enabled = 1;
    p.loudness = (uint32_t) rv::LoudnessMode::Leveler;
    p.targetLufs = -14; p.maxBoostDb = 20; p.jumpGuardDb = 1.5f; p.quietToNewSec = 1.0f; p.riseDbPerSec = 3;
    p.fidelity = 50; p.ambience = 0; p.surround = 0; p.bass = 0;
    const float eq[10] = { 0, 6, 3, 6.7f, 8.3f, 8.3f, 8.3f, 6.6f, 6.3f, 7 };
    std::memcpy (p.eqDb, eq, sizeof (eq));
    rv::writeParams (*shared, p);

    // Lock and process in 10 ms buffers.
    const UINT32 block = (UINT32) (a.rate / 100);
    std::vector<float> buf ((size_t) block * 2);
    APO_CONNECTION_DESCRIPTOR din { APO_CONNECTION_BUFFER_TYPE_EXTERNAL, (UINT_PTR) buf.data(), block, &flt, APO_CONNECTION_DESCRIPTOR_SIGNATURE };
    APO_CONNECTION_DESCRIPTOR dout = din;
    APO_CONNECTION_DESCRIPTOR* pin = &din;
    APO_CONNECTION_DESCRIPTOR* pout = &dout;
    CHECK (SUCCEEDED (cfg->LockForProcess (1, &pin, 1, &pout)), "LockForProcess");
    CHECK (cfg->LockForProcess (1, &pin, 1, &pout) == APOERR_APO_LOCKED, "second LockForProcess refused");
    HNSTIME lat = 0;
    apo->GetLatency (&lat);
    CHECK (lat > 0 && lat < 100000, "latency %.2f ms", lat / 1.0e4);

    const UINT32 frames = (UINT32) (a.data.size() / 2);
    std::vector<float> out (a.data.size());
    APO_CONNECTION_PROPERTY cin { (UINT_PTR) buf.data(), 0, BUFFER_VALID, APO_CONNECTION_PROPERTY_SIGNATURE };
    APO_CONNECTION_PROPERTY cout = cin;
    APO_CONNECTION_PROPERTY* ppin = &cin;
    APO_CONNECTION_PROPERTY* ppout = &cout;
    for (UINT32 pos = 0; pos < frames; pos += block)
    {
        const UINT32 n = std::min (block, frames - pos);
        std::memcpy (buf.data(), &a.data[(size_t) pos * 2], (size_t) n * 2 * sizeof (float));
        cin.u32ValidFrameCount = n;
        cin.u32BufferFlags = BUFFER_VALID;
        rt->APOProcess (1, &ppin, 1, &ppout);
        std::memcpy (&out[(size_t) pos * 2], buf.data(), (size_t) n * 2 * sizeof (float));
    }
    double maxDiff = 0;
    for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (out[i] - expected.data[i]));
    CHECK (maxDiff == 0.0, "output equals the offline engine (max diff %.2e)", maxDiff);
    const auto& t = shared->telemetry;
    CHECK (t.heartbeat > 0 && t.sampleRate == (uint32_t) a.rate && t.channels == 2 && t.paramsSource == 2,
           "telemetry: %u buffers, %u Hz, source %u", t.heartbeat, t.sampleRate, t.paramsSource);
    CHECK (t.framesWritten > 10 && t.frames[(t.framesWritten - 1) % rv::kFrameRing].seq == t.framesWritten,
           "analysis frames: %u", t.framesWritten);
    CHECK (t.outShortTerm > -16.5f && t.outShortTerm < -11.5f, "output short-term loudness %.1f LUFS (target -14)", t.outShortTerm);

    // Silent buffers stay silent.
    std::fill (buf.begin(), buf.end(), 0.5f);
    cin.u32BufferFlags = BUFFER_SILENT;
    cin.u32ValidFrameCount = block;
    rt->APOProcess (1, &ppin, 1, &ppout);
    bool silent = cout.u32BufferFlags == BUFFER_SILENT;
    for (float v : buf) silent &= v == 0.0f;
    CHECK (silent, "silent buffer -> silent output");

    // Bypass: enabled = 0 passes audio through untouched.
    p.enabled = 0;
    rv::writeParams (*shared, p);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = (float) std::sin (i * 0.01) * 0.3f;
    const std::vector<float> before = buf;
    cin.u32BufferFlags = BUFFER_VALID;
    rt->APOProcess (1, &ppin, 1, &ppout);
    CHECK (buf == before, "disabled -> untouched");

    CHECK (SUCCEEDED (cfg->UnlockForProcess()), "UnlockForProcess");
    CHECK (cfg->UnlockForProcess() == APOERR_ALREADY_UNLOCKED, "second Unlock refused");
    CHECK (SUCCEEDED (cfg->LockForProcess (1, &pin, 1, &pout)) && SUCCEEDED (cfg->UnlockForProcess()), "relock");

    UnmapViewOfFile (shared);
    CloseHandle (h);
    sfx->Release(); cfg->Release(); rt->Release(); apo->Release(); factory->Release();
    using CanUnload = HRESULT (__stdcall*)();
    auto canUnload = reinterpret_cast<CanUnload> (GetProcAddress (dll, "DllCanUnloadNow"));
    CHECK (canUnload && canUnload() == S_OK, "DLL can unload after release");
    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
