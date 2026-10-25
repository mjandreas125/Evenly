#include "KeepAlive.h"
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace rv
{
void KeepAlive::start (const std::wstring& endpointGuid)
{
    stop();
    quit = false;
    worker = std::thread ([this, endpointGuid] { run (endpointGuid); });
}

void KeepAlive::stop()
{
    quit = true;
    if (worker.joinable()) worker.join();
}

void KeepAlive::run (std::wstring guid)
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    while (! quit)
    {
        IMMDeviceEnumerator* en = nullptr;
        IMMDevice* dev = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* render = nullptr;
        WAVEFORMATEX* fmt = nullptr;
        bool ok = SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))
               && SUCCEEDED (en->GetDevice ((L"{0.0.0.00000000}." + guid).c_str(), &dev))
               && SUCCEEDED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client))
               && SUCCEEDED (client->GetMixFormat (&fmt))
               && SUCCEEDED (client->Initialize (AUDCLNT_SHAREMODE_SHARED, 0, 2000000 /* 200 ms */, 0, fmt, nullptr))
               && SUCCEEDED (client->GetService (__uuidof (IAudioRenderClient), (void**) &render))
               && SUCCEEDED (client->Start());
        UINT32 bufferFrames = 0;
        if (ok) client->GetBufferSize (&bufferFrames);
        active = ok;
        // Keep the buffer topped up with silence; if the device goes away, start over.
        while (ok && ! quit)
        {
            UINT32 padding = 0;
            if (FAILED (client->GetCurrentPadding (&padding))) break;
            const UINT32 free = bufferFrames - padding;
            BYTE* data = nullptr;
            if (free > 0 && SUCCEEDED (render->GetBuffer (free, &data))) render->ReleaseBuffer (free, AUDCLNT_BUFFERFLAGS_SILENT);
            Sleep (50);
        }
        active = false;
        if (client) client->Stop();
        if (render) render->Release();
        if (fmt) CoTaskMemFree (fmt);
        if (client) client->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        for (int i = 0; i < 30 && ! quit; ++i) Sleep (100);   // retry in 3 s
    }
    CoUninitialize();
}
} // namespace rv
