// Stands in for the effect inside Windows audio, for testing the app without sound hardware:
// creates the same shared block as the effect for <endpoint>, runs a WAV file (looped) through
// the engine in real time, takes the app's settings and reports telemetry and analysis frames.
// Nothing is played. Run elevated (Global\ objects).
//   rovno_fakefx <endpoint guid> <file.wav> [seconds]
#include "../dsp/Engine.h"
#include "../tools/Wav.h"
#include <windows.h>
#include <sddl.h>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

int wmain (int argc, wchar_t** argv)
{
    if (argc < 3) { std::printf ("usage: rovno_fakefx <endpoint guid> <file.wav> [seconds]\n"); return 1; }
    const std::wstring ep = argv[1];
    char path[MAX_PATH];
    WideCharToMultiByte (CP_ACP, 0, argv[2], -1, path, MAX_PATH, nullptr, nullptr);
    rv::wav::Audio a;
    if (! rv::wav::read (path, a) || a.channels != 2) { std::printf ("need a stereo wav\n"); return 2; }
    const int seconds = argc > 3 ? _wtoi (argv[3]) : 60;

    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW (L"D:(A;;GA;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr);
    SECURITY_ATTRIBUTES sa { sizeof (sa), sd, FALSE };
    HANDLE map = CreateFileMappingW (INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof (rv::SharedBlock), (L"Global\\Rovno." + ep).c_str());
    if (! map) { std::printf ("cannot create the shared block (%lu); run elevated\n", GetLastError()); return 3; }
    auto* b = static_cast<rv::SharedBlock*> (MapViewOfFile (map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (rv::SharedBlock)));
    std::memset (b, 0, sizeof (rv::SharedBlock));
    b->magic = rv::kSharedMagic;
    b->version = rv::kSharedVersion;

    rv::Engine engine;
    engine.prepare (a.rate, 2);
    rv::EngineParams params;
    params.enabled = 0;
    const int block = a.rate / 100, frames = (int) (a.data.size() / 2);
    std::vector<float> buf ((size_t) block * 2);
    auto next = std::chrono::steady_clock::now();
    int pos = 0;
    const long long total = (long long) seconds * 100;
    for (long long n = 0; n < total; ++n)
    {
        for (int i = 0; i < block; ++i, pos = (pos + 1) % frames)
        {
            buf[(size_t) i * 2] = a.data[(size_t) pos * 2];
            buf[(size_t) i * 2 + 1] = a.data[(size_t) pos * 2 + 1];
        }
        if (b->paramsWritten) { rv::EngineParams p; if (rv::tryReadParams (*b, p)) params = p; }
        engine.setParams (params);
        engine.process (buf.data(), block, &b->telemetry);
        b->telemetry.paramsSource = b->paramsWritten ? 2 : 0;
        b->telemetry.engineBuild = rv::kSharedVersion;
        if (n % 200 == 0)
        {
            std::printf ("%3lld s  enabled %u  in %.1f  out %.1f LUFS  leveler %+.1f dB  tone", n / 100, params.enabled,
                         b->telemetry.inShortTerm, b->telemetry.outShortTerm, b->telemetry.levelerGainDb);
            for (float t : params.toneDb) std::printf (" %+.1f", t);
            std::printf ("  fx %d/%d/%d/%d\n", params.fidelity, params.bass, params.surround, params.ambience);
        }
        next += std::chrono::milliseconds (10);
        std::this_thread::sleep_until (next);
    }
    UnmapViewOfFile (b);
    CloseHandle (map);
    LocalFree (sd);
    return 0;
}
