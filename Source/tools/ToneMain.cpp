// Offline run of the tone corrector exactly as the app would drive it in real time: the engine's
// analysis frames go to the corrector, its correction goes back to the engine as toneDb.
//   rovno_tone in.wav out.wav --content speech|music [--strength 0.8] [--log corrections.csv]
// Loudness processing is off so the output shows the tone change alone.
#include "../brain/ToneCorrector.h"
#include "../dsp/Engine.h"
#include "Wav.h"
#include <cstdio>
#include <memory>
#include <string>

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: rovno_tone in.wav out.wav --content speech|music [--strength x] [--log file.csv]\n"); return 1; }
    rv::wav::Audio a;
    if (! rv::wav::read (argv[1], a)) { std::printf ("cannot read %s\n", argv[1]); return 2; }
    rv::ToneCorrector tc;
    const char* logPath = nullptr;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string k = argv[i];
        if (k == "--content") tc.setContent (std::string (argv[i + 1]) == "music" ? rv::Content::Music : rv::Content::Speech);
        else if (k == "--strength") tc.setStrength ((float) std::atof (argv[i + 1]));
        else if (k == "--width") tc.setWidth ((float) std::atof (argv[i + 1]));
        else if (k == "--log") logPath = argv[i + 1];
    }
    rv::Engine engine;
    engine.prepare (a.rate, a.channels);
    rv::EngineParams p;
    p.loudness = (uint32_t) rv::LoudnessMode::Off;
    p.ceilingDb = 0;
    engine.setParams (p);
    auto tel = std::make_unique<rv::Telemetry>();
    std::memset (tel.get(), 0, sizeof (rv::Telemetry));
    FILE* log = logPath ? std::fopen (logPath, "w") : nullptr;
    uint32_t seen = 0;
    const int block = a.rate / 100, frames = (int) (a.data.size() / (size_t) a.channels);
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min (block, frames - pos);
        engine.process (a.data.data() + (size_t) pos * (size_t) a.channels, n, tel.get());
        for (; seen < tel->framesWritten; ++seen)
            if (tc.push (tel->frames[seen % rv::kFrameRing]))
            {
                std::memcpy (p.toneDb, tc.correction(), sizeof (p.toneDb));
                engine.setParams (p);
                if (log)
                {
                    std::fprintf (log, "%.2f", pos / (double) a.rate);
                    for (int b = 0; b < 10; ++b) std::fprintf (log, ",%.1f", p.toneDb[b]);
                    std::fprintf (log, "\n");
                }
            }
    }
    if (log)
    {
        std::fprintf (log, "shape");
        for (int b = 0; b < rv::kBands; ++b) std::fprintf (log, ",%.2f", tc.spectrumShape()[b]);
        std::fprintf (log, "\n");
        std::fclose (log);
    }
    rv::wav::write (argv[2], a);
    std::printf ("final correction:");
    for (int b = 0; b < 10; ++b) std::printf (" %+.1f", tc.correction()[b]);
    std::printf ("  (confidence %.2f)\n", tc.confidence());
    return 0;
}
