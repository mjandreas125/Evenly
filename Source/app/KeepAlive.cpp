#include "KeepAlive.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

namespace rv
{
namespace
{
// An output that is simply asleep (monitor off) is normal and comes back on its own; anything else
// means we are doing something wrong and should be shown to the user rather than retried silently.
bool isTemporary (HRESULT hr)
{
    return hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED
           || hr == AUDCLNT_E_SERVICE_NOT_RUNNING || hr == (HRESULT) 0x80070490 /* not found */;
}

// The sample size the hardware really gets (the shared-mode mix is always float).
HRESULT readDeviceBits (IMMDevice* dev, bool& is16Bit)
{
    IPropertyStore* props = nullptr;
    HRESULT hr = dev->OpenPropertyStore (STGM_READ, &props);
    if (FAILED (hr)) return S_OK;   // not fatal: assume 24-bit
    PROPVARIANT v;
    PropVariantInit (&v);
    if (SUCCEEDED (props->GetValue (PKEY_AudioEngine_DeviceFormat, &v)) && v.vt == VT_BLOB && v.blob.cbSize >= sizeof (WAVEFORMATEX))
        is16Bit = reinterpret_cast<const WAVEFORMATEX*> (v.blob.pBlobData)->wBitsPerSample <= 16;
    PropVariantClear (&v);
    props->Release();
    return S_OK;
}

// Level of the keep-awake tone in dBFS: -60 unless %APPDATA%\Rovno\keep-awake-db.txt holds another number
// (read again every half second, so that the level can be tuned to a monitor while listening).
double toneAmplitude()
{
    double db = -60;
    wchar_t dir[MAX_PATH] {};
    if (GetEnvironmentVariableW (L"APPDATA", dir, MAX_PATH) > 0)
    {
        FILE* f = _wfopen ((std::wstring (dir) + L"/Rovno/keep-awake-db.txt").c_str(), L"r");
        if (f)
        {
            double v = 0;
            if (fscanf (f, "%lf", &v) == 1 && v <= -20 && v >= -100) db = v;
            fclose (f);
        }
    }
    return std::pow (10.0, db / 20.0);
}
} // namespace

void KeepAlive::start (const std::wstring& endpointId)
{
    stop();
    if (endpointId.empty()) return;
    quitEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
    state = Status::Waiting;
    worker = std::thread ([this, endpointId] { run (endpointId); });
}

void KeepAlive::stop()
{
    if (quitEvent) SetEvent (quitEvent);
    if (worker.joinable()) worker.join();
    if (quitEvent) { CloseHandle (quitEvent); quitEvent = nullptr; }
    state = Status::Off;
}

bool KeepAlive::sleepOrQuit (int ms)
{
    return ! quitEvent || WaitForSingleObject (quitEvent, (DWORD) ms) == WAIT_TIMEOUT;
}

void KeepAlive::run (std::wstring id)
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    int backoffMs = 1000;                                  // grows while the output is unavailable
    while (sleepOrQuit (0))
    {
        IMMDeviceEnumerator* en = nullptr;
        IMMDevice* dev = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* render = nullptr;
        WAVEFORMATEX* fmt = nullptr;
        UINT32 bufferFrames = 0;
        bool deviceIs16Bit = false;
        HRESULT hr = S_OK;
        const char* step = "";

        auto call = [&hr, &step] (const char* name, HRESULT r) { step = name; hr = r; return SUCCEEDED (r); };

        const bool opened =
            call ("CoCreateInstance", CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                                        __uuidof (IMMDeviceEnumerator), (void**) &en))
            && call ("GetDevice", en->GetDevice (id.c_str(), &dev))
            && call ("Activate", dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client))
            && call ("GetMixFormat", client->GetMixFormat (&fmt))
            && call ("DeviceFormat", readDeviceBits (dev, deviceIs16Bit))
            && call ("Initialize", client->Initialize (AUDCLNT_SHAREMODE_SHARED, 0, 2000000 /* 200 ms */, 0, fmt, nullptr))
            && call ("GetService", client->GetService (__uuidof (IAudioRenderClient), (void**) &render))
            && call ("Start", client->Start())
            && call ("GetBufferSize", client->GetBufferSize (&bufferFrames));

        if (opened)
        {
            lastHr = 0;
            lastStep = "";
            state = Status::Holding;
            backoffMs = 1000;
            // Keep the buffer topped up until the output goes away or we are stopped. Pure zeros are not
            // enough: many monitors, TVs and HDMI/DisplayPort codecs mute at digital silence and need a
            // moment to wake up when sound comes back (the first moments of the next sound are lost).
            // So the stream carries a constant, inaudible tone (see below), not just the smallest possible sample.
            const bool floatMix = fmt && (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->wBitsPerSample == 32));
            const UINT32 channels = fmt ? fmt->nChannels : 2;
            double phase = 0;
            const double kStep = 6.283185307179586 * 12.0 / (fmt ? fmt->nSamplesPerSec : 48000);
            double amplitude = 0, target = toneAmplitude();
            ULONGLONG lastRead = GetTickCount64();
            while (sleepOrQuit (50))
            {
                UINT32 padding = 0;
                if (FAILED (hr = client->GetCurrentPadding (&padding))) { step = "GetCurrentPadding"; break; }
                const UINT32 freeFrames = bufferFrames > padding ? bufferFrames - padding : 0;
                if (freeFrames == 0) continue;
                BYTE* data = nullptr;
                if (FAILED (hr = render->GetBuffer (freeFrames, &data))) { step = "GetBuffer"; break; }
                const ULONGLONG now = GetTickCount64();
                if (now - lastRead >= 500) { lastRead = now; target = toneAmplitude(); }
                if (floatMix)
                {
                    // A subsonic tone: 12 Hz, quiet (toneAmplitude). No speaker reproduces it, but to the digital input of a monitor
                    // it is a real signal. The lowest-level noise (one 16-bit step, as pulses or as dither) was
                    // not enough on the ASUS MX34VQ: it muted in pauses and woke up a few hundred milliseconds late.
                    auto* out = reinterpret_cast<float*> (data);
                    for (UINT32 i = 0; i < freeFrames; ++i)
                    {
                        amplitude += (target - amplitude) * 0.0002;   // glides, so that a change never clicks
                        const float v = (float) (amplitude * std::sin (phase));
                        phase += kStep;
                        if (phase > 6.283185307179586) phase -= 6.283185307179586;
                        for (UINT32 c = 0; c < channels; ++c) out[(size_t) i * channels + c] = v;
                    }
                    hr = render->ReleaseBuffer (freeFrames, 0);
                }
                else hr = render->ReleaseBuffer (freeFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                if (FAILED (hr)) { step = "ReleaseBuffer"; break; }
            }
        }

        if (client) client->Stop();
        if (render) render->Release();
        if (fmt) CoTaskMemFree (fmt);
        if (client) client->Release();
        if (dev) dev->Release();
        if (en) en->Release();

        if (! sleepOrQuit (0)) break;

        lastHr = (long) hr;
        lastStep = step;
        state = isTemporary (hr) || opened ? Status::Waiting : Status::Failed;
        // Opening a stream is not free (Windows builds the whole effect chain for it), so do not
        // hammer the output while it is unavailable: wait longer after every failed attempt.
        if (! sleepOrQuit (backoffMs)) break;
        backoffMs = backoffMs < 30000 ? backoffMs * 2 : 30000;
    }
    state = Status::Off;
    CoUninitialize();
}
} // namespace rv
