#include "KeepAlive.h"
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

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
        HRESULT hr = S_OK;
        const char* step = "";

        auto call = [&hr, &step] (const char* name, HRESULT r) { step = name; hr = r; return SUCCEEDED (r); };

        const bool opened =
            call ("CoCreateInstance", CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                                        __uuidof (IMMDeviceEnumerator), (void**) &en))
            && call ("GetDevice", en->GetDevice (id.c_str(), &dev))
            && call ("Activate", dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client))
            && call ("GetMixFormat", client->GetMixFormat (&fmt))
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
            // Keep the buffer topped up with silence until the output goes away or we are stopped.
            while (sleepOrQuit (50))
            {
                UINT32 padding = 0;
                if (FAILED (hr = client->GetCurrentPadding (&padding))) { step = "GetCurrentPadding"; break; }
                const UINT32 freeFrames = bufferFrames > padding ? bufferFrames - padding : 0;
                if (freeFrames == 0) continue;
                BYTE* data = nullptr;
                if (FAILED (hr = render->GetBuffer (freeFrames, &data))) { step = "GetBuffer"; break; }
                if (FAILED (hr = render->ReleaseBuffer (freeFrames, AUDCLNT_BUFFERFLAGS_SILENT))) { step = "ReleaseBuffer"; break; }
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
