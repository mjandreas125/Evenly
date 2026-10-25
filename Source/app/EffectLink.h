#pragma once
// The app's side of the shared block with the effect running inside Windows audio for one output
// device: sends settings, reads the effect's measurements and analysis frames, and keeps the
// settings on disk so the effect starts with them after a reboot (before the app runs).
#include "../dsp/Shared.h"
#include <windows.h>
#include <string>
#include <vector>

namespace rv
{
class EffectLink
{
public:
    ~EffectLink() { close(); }

    void setEndpoint (const std::wstring& guid)
    {
        if (guid == endpoint) return;
        close();
        endpoint = guid;
        lastFrame = 0;
    }
    const std::wstring& endpointGuid() const { return endpoint; }

    // True while the effect is loaded for this device (the block exists).
    bool connected()
    {
        if (block) return true;
        if (endpoint.empty()) return false;
        const DWORD now = GetTickCount();
        if (now - lastTry < 1000) return false;   // do not hammer while the effect is not there
        lastTry = now;
        mapping = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, (L"Global\\Rovno." + endpoint).c_str());
        if (! mapping) return false;
        block = static_cast<SharedBlock*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedBlock)));
        if (! block || block->magic != kSharedMagic || block->version != kSharedVersion) { close(); return false; }
        lastFrame = block->telemetry.framesWritten;
        return true;
    }

    // Is sound going through the effect right now (heartbeat moving)?
    bool processing()
    {
        if (! connected()) return false;
        const uint32_t hb = block->telemetry.heartbeat;
        const DWORD now = GetTickCount();
        if (hb != lastBeat) { lastBeat = hb; lastBeatAt = now; }
        return now - lastBeatAt < 1500;
    }

    void send (const EngineParams& p)
    {
        if (connected()) writeParams (*block, p);
        if (std::memcmp (&p, &saved, sizeof (p)) != 0) { saved = p; dirty = true; dirtyAt = GetTickCount(); }
    }

    // Saves the settings at most once every 2 s (the effect reads them when Windows starts it).
    void flushToDisk (bool force = false)
    {
        if (! dirty || endpoint.empty() || (! force && GetTickCount() - dirtyAt < 2000)) return;
        wchar_t dir[MAX_PATH] {};
        if (! GetEnvironmentVariableW (L"ProgramData", dir, MAX_PATH)) return;
        const std::wstring folder = std::wstring (dir) + L"\\Rovno";
        CreateDirectoryW (folder.c_str(), nullptr);
        const std::wstring path = folder + L"\\" + endpoint + L".params", tmp = path + L".tmp";
        HANDLE f = CreateFileW (tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        const uint32_t head[3] = { kSharedMagic, kSharedVersion, (uint32_t) sizeof (EngineParams) };
        DWORD w;
        const bool ok = WriteFile (f, head, sizeof (head), &w, nullptr) && WriteFile (f, &saved, sizeof (saved), &w, nullptr);
        CloseHandle (f);
        if (ok && MoveFileExW (tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) dirty = false;
    }

    const Telemetry* telemetry() { return connected() ? &block->telemetry : nullptr; }

    // Analysis frames written since the last call (oldest first).
    void newFrames (std::vector<AnalysisFrame>& out)
    {
        out.clear();
        if (! connected()) return;
        const Telemetry& t = block->telemetry;
        const uint32_t written = t.framesWritten;
        if (written - lastFrame > (uint32_t) kFrameRing) lastFrame = written - kFrameRing;   // we fell behind
        for (; lastFrame < written; ++lastFrame)
        {
            const AnalysisFrame& slot = t.frames[lastFrame % kFrameRing];
            AnalysisFrame copy;
            std::memcpy (&copy, &slot, sizeof (copy));
            if (copy.seq == lastFrame + 1) out.push_back (copy);   // skip a frame being overwritten
        }
    }

private:
    void close()
    {
        if (block) UnmapViewOfFile (block);
        if (mapping) CloseHandle (mapping);
        block = nullptr;
        mapping = nullptr;
    }

    std::wstring endpoint;
    HANDLE mapping = nullptr;
    SharedBlock* block = nullptr;
    DWORD lastTry = 0, lastBeatAt = 0, dirtyAt = 0;
    uint32_t lastBeat = 0, lastFrame = 0;
    EngineParams saved {};
    bool dirty = false;
};
} // namespace rv
