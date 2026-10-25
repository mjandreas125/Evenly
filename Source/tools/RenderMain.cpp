// Offline renderer: runs a WAV file through the Rovno engine exactly as the audio effect would.
//   rovno_render in.wav out.wav [--fx fid,amb,surr,boost,bass] [--eq g1,...,g10] [--tone g1,...,g10]
//                [--loudness off|leveler|fxsound] [--target -16] [--block 480] [--frames out.csv]
// --frames writes the analysis frames (what the app would see) as CSV.
#include "../dsp/Engine.h"
#include "Wav.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

static std::vector<float> numbers (const char* s)
{
    std::vector<float> v;
    std::stringstream ss (s);
    std::string item;
    while (std::getline (ss, item, ',')) v.push_back ((float) std::atof (item.c_str()));
    return v;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: rovno_render in.wav out.wav [--fx fid,amb,surr,boost,bass] [--eq ...10] [--tone ...10] "
                     "[--loudness off|leveler|fxsound] [--target lufs] [--block n] [--frames out.csv]\n");
        return 1;
    }
    rv::wav::Audio a;
    if (! rv::wav::read (argv[1], a)) { std::printf ("cannot read %s\n", argv[1]); return 2; }
    rv::EngineParams p;
    int block = 480;
    const char* framesCsv = nullptr;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string k = argv[i];
        const char* v = argv[i + 1];
        if (k == "--fx")
        {
            const auto n = numbers (v);
            if (n.size() == 5) { p.fidelity = (int) n[0]; p.ambience = (int) n[1]; p.surround = (int) n[2]; p.boost = (int) n[3]; p.bass = (int) n[4]; }
        }
        else if (k == "--eq") { const auto n = numbers (v); for (size_t j = 0; j < 10 && j < n.size(); ++j) p.eqDb[j] = n[j]; }
        else if (k == "--tone") { const auto n = numbers (v); for (size_t j = 0; j < 10 && j < n.size(); ++j) p.toneDb[j] = n[j]; }
        else if (k == "--loudness") p.loudness = std::string (v) == "off" ? 0u : (std::string (v) == "fxsound" ? 2u : 1u);
        else if (k == "--target") p.targetLufs = (float) std::atof (v);
        else if (k == "--maxboost") p.maxBoostDb = (float) std::atof (v);
        else if (k == "--guard") p.jumpGuardDb = (float) std::atof (v);
        else if (k == "--rise") p.riseDbPerSec = (float) std::atof (v);
        else if (k == "--quietnew") p.quietToNewSec = (float) std::atof (v);
        else if (k == "--block") block = std::max (1, std::atoi (v));
        else if (k == "--frames") framesCsv = v;
    }

    rv::Engine engine;
    engine.prepare (a.rate, a.channels);
    engine.setParams (p);
    auto tel = std::make_unique<rv::Telemetry>();
    std::memset (tel.get(), 0, sizeof (rv::Telemetry));
    FILE* csv = framesCsv ? std::fopen (framesCsv, "w") : nullptr;
    uint32_t seen = 0;
    const int frames = (int) (a.data.size() / (size_t) a.channels);
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min (block, frames - pos);
        engine.process (a.data.data() + (size_t) pos * (size_t) a.channels, n, tel.get());
        for (; csv && seen < tel->framesWritten; ++seen)
        {
            const auto& f = tel->frames[seen % rv::kFrameRing];
            std::fprintf (csv, "%u,%.3f,%.4f,%.2f", f.seq, f.lufs, f.peak, tel->levelerGainDb);
            for (float b : f.bandsDb) std::fprintf (csv, ",%.2f", b);
            std::fprintf (csv, "\n");
        }
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (csv) std::fclose (csv);
    if (! rv::wav::write (argv[2], a)) { std::printf ("cannot write %s\n", argv[2]); return 3; }
    std::printf ("ok %d Hz %d ch, %.1f s, %.0fx real time, latency %d, in %.1f LUFS(S) out %.1f LUFS(S), leveler %.1f dB\n",
                 a.rate, a.channels, frames / (double) a.rate, frames / (double) a.rate / std::max (secs, 1e-9),
                 engine.latencySamples(), tel->inShortTerm, tel->outShortTerm, tel->levelerGainDb);
    return 0;
}
