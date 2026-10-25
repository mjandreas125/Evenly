#include "SourceDetector.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <algorithm>
#include <chrono>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

namespace rv
{
namespace wmc = winrt::Windows::Media::Control;

namespace
{
template <typename T> void release (T*& p) { if (p) { p->Release(); p = nullptr; } }

std::wstring exeName (DWORD pid)
{
    std::wstring out;
    if (HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
    {
        wchar_t path[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW (h, 0, path, &n))
        {
            out = path;
            const auto slash = out.find_last_of (L"\\/");
            if (slash != std::wstring::npos) out = out.substr (slash + 1);
        }
        CloseHandle (h);
    }
    return out;
}

struct TitleSearch { DWORD pid; std::wstring title; size_t best = 0; };

BOOL CALLBACK findWindow (HWND hwnd, LPARAM lp)
{
    auto* s = reinterpret_cast<TitleSearch*> (lp);
    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    if (pid != s->pid || ! IsWindowVisible (hwnd) || GetWindow (hwnd, GW_OWNER) != nullptr) return TRUE;
    wchar_t buf[512];
    const int n = GetWindowTextW (hwnd, buf, 512);
    if (n > 0 && (size_t) n > s->best) { s->title.assign (buf, (size_t) n); s->best = (size_t) n; }
    return TRUE;
}

std::wstring windowTitle (DWORD pid)
{
    TitleSearch s { pid, {} };
    EnumWindows (findWindow, reinterpret_cast<LPARAM> (&s));
    return s.title;
}
} // namespace

struct SourceDetector::Impl
{
    IMMDeviceEnumerator* enumerator = nullptr;
    wmc::GlobalSystemMediaTransportControlsSessionManager manager { nullptr };
    std::chrono::steady_clock::time_point managerAt {};

    Impl()
    {
        try { winrt::init_apartment (winrt::apartment_type::multi_threaded); } catch (...) {}
        CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &enumerator);
    }
    ~Impl() { release (enumerator); }

    // Programs with sound on any active output (apps play to the default device, which may not
    // be the processed one while another enhancer is in the chain).
    void sessions (std::vector<PlayingApp>& out)
    {
        if (! enumerator) return;
        IMMDeviceCollection* devices = nullptr;
        if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &devices))) return;
        UINT count = 0;
        devices->GetCount (&count);
        const DWORD self = GetCurrentProcessId();
        for (UINT d = 0; d < count; ++d)
        {
            IMMDevice* dev = nullptr;
            IAudioSessionManager2* mgr = nullptr;
            IAudioSessionEnumerator* list = nullptr;
            if (SUCCEEDED (devices->Item (d, &dev)) && SUCCEEDED (dev->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**) &mgr))
                && SUCCEEDED (mgr->GetSessionEnumerator (&list)))
            {
                int n = 0;
                list->GetCount (&n);
                for (int i = 0; i < n; ++i)
                {
                    IAudioSessionControl* ctl = nullptr;
                    IAudioSessionControl2* ctl2 = nullptr;
                    IAudioMeterInformation* meter = nullptr;
                    if (SUCCEEDED (list->GetSession (i, &ctl)) && SUCCEEDED (ctl->QueryInterface (__uuidof (IAudioSessionControl2), (void**) &ctl2)))
                    {
                        AudioSessionState state {};
                        DWORD pid = 0;
                        ctl2->GetState (&state);
                        ctl2->GetProcessId (&pid);
                        float peak = 0;
                        if (SUCCEEDED (ctl->QueryInterface (__uuidof (IAudioMeterInformation), (void**) &meter))) meter->GetPeakValue (&peak);
                        if (state == AudioSessionStateActive && pid != 0 && pid != self && ctl2->IsSystemSoundsSession() != S_OK)
                        {
                            auto it = std::find_if (out.begin(), out.end(), [&] (const PlayingApp& a) { return a.pid == pid; });
                            if (it == out.end()) out.push_back ({ pid, exeName (pid), windowTitle (pid), peak });
                            else it->peak = std::max (it->peak, peak);
                        }
                    }
                    release (meter); release (ctl2); release (ctl);
                }
            }
            release (list); release (mgr); release (dev);
        }
        devices->Release();
        std::sort (out.begin(), out.end(), [] (const PlayingApp& a, const PlayingApp& b) { return a.peak > b.peak; });
    }

    void media (NowPlaying& np)
    {
        try
        {
            const auto now = std::chrono::steady_clock::now();
            if (! manager || now - managerAt > std::chrono::minutes (5))
            {
                manager = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                managerAt = now;
            }
            if (! manager) return;
            // Prefer the session that is actually playing; Windows' "current" one may be paused.
            wmc::GlobalSystemMediaTransportControlsSession chosen { nullptr };
            for (const auto& s : manager.GetSessions())
                if (s.GetPlaybackInfo().PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) { chosen = s; break; }
            if (! chosen) chosen = manager.GetCurrentSession();
            if (! chosen) return;
            const auto props = chosen.TryGetMediaPropertiesAsync().get();
            np.present = true;
            np.appId = chosen.SourceAppUserModelId().c_str();
            np.title = props.Title().c_str();
            np.artist = props.Artist().c_str();
            np.album = props.AlbumTitle().c_str();
            if (const auto t = props.PlaybackType()) np.playbackType = (int) t.Value();
            np.playing = chosen.GetPlaybackInfo().PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        }
        catch (...)
        {
            manager = nullptr;   // e.g. the media service restarted; ask again next time
        }
    }
};

SourceDetector::SourceDetector() : impl (new Impl()) {}
SourceDetector::~SourceDetector() { delete impl; }

SourceSnapshot SourceDetector::poll (const std::wstring&)
{
    SourceSnapshot s;
    impl->sessions (s.apps);
    impl->media (s.media);
    return s;
}
} // namespace rv
