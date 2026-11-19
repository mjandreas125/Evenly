#pragma once
// Listens to what the computer is playing (WASAPI loopback of the default output; it makes no sound and changes nothing) and hands
// the mix to VoiceDsp. Started when a picture needs the voice, stopped when nobody asks for it any more.
#include "VoiceDsp.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

namespace rv
{
class VoiceCapture
{
public:
    static VoiceCapture& get() { static VoiceCapture c; return c; }
    ~VoiceCapture() { stop(); }

    // Every picture that shows the voice calls this each frame: the listening keeps going while somebody is asking.
    VoiceFeatures features()
    {
        lastAsk = std::chrono::steady_clock::now();
        if (! running.exchange (true)) startThread();
        std::lock_guard<std::mutex> g (m);
        return out;
    }
    bool listening() const { return live.load(); }
    // the last few milliseconds of the sound as 256 points between -1 and 1 (starting at a rising zero crossing, so the line stands still)
    bool waveform (float* out512)   // 256 points of everything, then 256 points of the voice range
    {
        lastAsk = std::chrono::steady_clock::now();
        if (! running.exchange (true)) startThread();
        std::lock_guard<std::mutex> g (m);
        if (! haveWave) return false;
        for (int i = 0; i < 512; ++i) out512[i] = wave[(size_t) i];
        return true;
    }
    // The loudness of the singing band (1-4 kHz) in 50 ms blocks since the last call, as (seconds ago, log energy); for lining the
    // words up with the voice (LyricAligner). Blocks of silence come through too (very low numbers).
    void takeSingingBand (std::vector<std::pair<double, float>>& dst)
    {
        dst.clear();
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> g (m);
        for (const auto& b : bandBlocks) dst.push_back ({ std::chrono::duration<double> (now - b.first).count(), b.second });
        bandBlocks.clear();
    }
    // when did sound last come in (to tell silence and a stopped song from a lost connection)
    double secondsSinceSound() const
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - lastSound.load()).count();
    }

private:
    void startThread()
    {
        if (th.joinable()) th.join();
        stopFlag = false;
        th = std::thread ([this] { loop(); });
    }
    void stop()
    {
        stopFlag = true;
        if (th.joinable()) th.join();
        running = false;
    }

    void loop()
    {
        const bool comOk = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
        while (! stopFlag)
        {
            session();
            live = false;
            for (int i = 0; i < 20 && ! stopFlag; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (100));   // then try again (device changed, sleep...)
            if (std::chrono::steady_clock::now() - lastAsk.load() > std::chrono::seconds (60)) break;
        }
        if (comOk) CoUninitialize();
        running = false;
    }

    void session()
    {
        IMMDeviceEnumerator* en = nullptr;
        IMMDevice* dev = nullptr;
        IAudioClient* ac = nullptr;
        IAudioCaptureClient* cap = nullptr;
        WAVEFORMATEX* fmt = nullptr;
        auto cleanup = [&]
        {
            if (cap) cap->Release();
            if (ac) { ac->Stop(); ac->Release(); }
            if (fmt) CoTaskMemFree (fmt);
            if (dev) dev->Release();
            if (en) en->Release();
        };
        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))) { cleanup(); return; }
        if (FAILED (en->GetDefaultAudioEndpoint (eRender, eConsole, &dev))) { cleanup(); return; }
        if (FAILED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &ac))) { cleanup(); return; }
        if (FAILED (ac->GetMixFormat (&fmt))) { cleanup(); return; }
        if (FAILED (ac->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr))) { cleanup(); return; }
        if (FAILED (ac->GetService (__uuidof (IAudioCaptureClient), (void**) &cap))) { cleanup(); return; }
        const int ch = std::max<int> (1, fmt->nChannels), bits = fmt->wBitsPerSample;
        bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            const auto* ex = (const WAVEFORMATEXTENSIBLE*) fmt;
            isFloat = ex->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
        }
        dsp.prepare ((double) fmt->nSamplesPerSec);
        vh1.design ((double) fmt->nSamplesPerSec, 200.0, true); vh2.design ((double) fmt->nSamplesPerSec, 200.0, true);
        vb1.design ((double) fmt->nSamplesPerSec, 3000.0, false); vb2.design ((double) fmt->nSamplesPerSec, 3000.0, false);
        sh1.design ((double) fmt->nSamplesPerSec, 1000.0, true); sh2.design ((double) fmt->nSamplesPerSec, 1000.0, true);
        sl1.design ((double) fmt->nSamplesPerSec, 4000.0, false); sl2.design ((double) fmt->nSamplesPerSec, 4000.0, false);
        const int blockLen = std::max (1, (int) (fmt->nSamplesPerSec / 20));      // 50 ms
        double bandAcc = 0.0; int bandN = 0;
        dsp.reset();
        if (FAILED (ac->Start())) { cleanup(); return; }
        live = true;

        std::vector<float> mono;
        while (! stopFlag)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            UINT32 packet = 0;
            if (FAILED (cap->GetNextPacketSize (&packet))) break;
            while (packet > 0 && ! stopFlag)
            {
                BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0;
                if (FAILED (cap->GetBuffer (&data, &n, &flags, nullptr, nullptr))) { cleanup(); return; }
                mono.assign (n, 0.0f);
                if (! (flags & AUDCLNT_BUFFERFLAGS_SILENT))
                {
                    for (UINT32 i = 0; i < n; ++i)
                    {
                        // the voice is mostly in the middle of the stereo picture: add the channels
                        float s = 0;
                        for (int c = 0; c < std::min (ch, 2); ++c)
                        {
                            if (isFloat && bits == 32) s += ((const float*) data)[i * ch + c];
                            else if (bits == 16) s += ((const short*) data)[i * ch + c] / 32768.0f;
                            else if (bits == 32) s += ((const int*) data)[i * ch + c] / 2147483648.0f;
                            else if (bits == 24) { const BYTE* p = data + ((size_t) i * ch + c) * 3; s += (float) (((p[2] << 24) | (p[1] << 16) | (p[0] << 8)) >> 8) / 8388608.0f; }
                        }
                        mono[i] = s / (float) std::min (ch, 2);
                    }
                    lastSound = std::chrono::steady_clock::now();
                }
                cap->ReleaseBuffer (n);
                for (UINT32 i = 0; i < n; ++i) { ring[ringPos] = mono[i]; ringV[ringPos] = vb2.run (vb1.run (vh2.run (vh1.run (mono[i])))); ringPos = (ringPos + 1) % ring.size(); }
                for (UINT32 i = 0; i < n; ++i)
                {
                    const float s = sl2.run (sl1.run (sh2.run (sh1.run (mono[i]))));
                    bandAcc += (double) s * s;
                    if (++bandN >= blockLen)
                    {
                        // the block ends at the newest sample of this packet; close enough for 50 ms blocks
                        const auto stamp = std::chrono::steady_clock::now() - std::chrono::microseconds ((long long) ((double) (n - 1 - i) * 1.0e6 / fmt->nSamplesPerSec));
                        std::lock_guard<std::mutex> g (m);
                        bandBlocks.push_back ({ stamp, (float) std::log (bandAcc / bandN + 1.0e-10) });
                        if (bandBlocks.size() > 2000) bandBlocks.erase (bandBlocks.begin(), bandBlocks.begin() + 1000);   // nobody is taking them
                        bandAcc = 0.0; bandN = 0;
                    }
                }
                makeWave();
                if (dsp.push (mono.data(), (int) n) > 0)
                {
                    std::lock_guard<std::mutex> g (m);
                    out = dsp.features();
                }
                if (FAILED (cap->GetNextPacketSize (&packet))) { cleanup(); return; }
            }
            // nothing has been heard for a while: let the numbers settle to "no voice" instead of freezing on the last note
            if (secondsSinceSound() > 1.5)
            {
                std::lock_guard<std::mutex> g (m);
                out.voiced *= 0.9f; out.intensity *= 0.9f; out.hold *= 0.9f; out.vibrato *= 0.9f; out.phraseAge = 0; out.endPulse *= 0.9f;
            }
            if (std::chrono::steady_clock::now() - lastAsk.load() > std::chrono::seconds (60)) break;
        }
        cleanup();
    }

    void makeWave()
    {
        // the newest 2048 samples; start at the first rising zero crossing of the voice range (so the lines stand still), every 4th sample
        const size_t N = ring.size();
        float tmp[2048], tv[2048];
        for (size_t i = 0; i < 2048; ++i) { tmp[i] = ring[(ringPos + N - 2048 + i) % N]; tv[i] = ringV[(ringPos + N - 2048 + i) % N]; }
        size_t s0 = 0;
        for (size_t i = 1; i < 900; ++i) if (tmp[i - 1] < 0 && tmp[i] >= 0) { s0 = i; break; }
        float pa[256], pv[256]; float mx = 1e-4f, mv = 1e-4f;
        for (size_t k = 0; k < 256; ++k)
        {
            const size_t i = s0 + k * 4;
            pa[k] = 0.25f * (tmp[i] + tmp[i + 1] + tmp[i + 2] + tmp[i + 3]);
            pv[k] = 0.25f * (tv[i] + tv[i + 1] + tv[i + 2] + tv[i + 3]);
            mx = std::max (mx, std::abs (pa[k])); mv = std::max (mv, std::abs (pv[k]));
        }
        waveMax = std::max (mx, waveMax * 0.998f);
        waveMaxV = std::max (mv, waveMaxV * 0.998f);
        std::lock_guard<std::mutex> g (m);
        for (size_t k = 0; k < 256; ++k)
        {
            wave[k] = std::max (-1.0f, std::min (1.0f, pa[k] / std::max (waveMax, 0.02f)));
            wave[256 + k] = std::max (-1.0f, std::min (1.0f, pv[k] / std::max (waveMaxV, 0.01f)));
        }
        haveWave = true;
    }

    std::thread th;
    std::mutex m;
    std::vector<float> ring = std::vector<float> (8192, 0.0f);
    size_t ringPos = 0;
    float waveMax = 0.05f;
    std::vector<float> ringV = std::vector<float> (8192, 0.0f);
    float waveMaxV = 0.05f;
    float wave[512] {};
    bool haveWave = false;
    struct Bq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void design (double fs, double fc, bool high) { const double w = 2 * 3.14159265358979 * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * 0.7071), a0 = 1 + al;
            const double B0 = high ? (1 + c) / 2 : (1 - c) / 2, B1 = high ? -(1 + c) : (1 - c);
            b0 = (float) (B0 / a0); b1 = (float) (B1 / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0); z1 = z2 = 0; }
        float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Bq vh1, vh2, vb1, vb2;
    Bq sh1, sh2, sl1, sl2;                                                          // the singing band, 1-4 kHz
    std::vector<std::pair<std::chrono::steady_clock::time_point, float>> bandBlocks;
    VoiceDsp dsp;
    VoiceFeatures out;
    std::atomic<bool> running { false }, live { false }, stopFlag { false };
    std::atomic<std::chrono::steady_clock::time_point> lastAsk { std::chrono::steady_clock::now() }, lastSound { std::chrono::steady_clock::now() };
};
} // namespace rv
