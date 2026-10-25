// Writes the speech/music features of a WAV, one row per second (4 s windows), for training:
//   rovno_features in.wav label out.csv      (label: 1 speech, 0 music)
#include "../brain/AudioFeatures.h"
#include "../dsp/Engine.h"
#include "Wav.h"
#include <cstdio>
#include <memory>

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("usage: rovno_features in.wav label out.csv\n"); return 1; }
    rv::wav::Audio a;
    if (! rv::wav::read (argv[1], a)) return 2;
    rv::Engine engine;
    engine.prepare (a.rate, a.channels);
    rv::EngineParams p;
    p.enabled = 0;
    engine.setParams (p);
    auto tel = std::make_unique<rv::Telemetry>();
    std::memset (tel.get(), 0, sizeof (rv::Telemetry));
    rv::FeatureWindow fw;
    FILE* out = std::fopen (argv[3], "a");
    uint32_t seen = 0;
    int sinceRow = 0;
    const int block = a.rate / 100, frames = (int) (a.data.size() / (size_t) a.channels);
    for (int pos = 0; pos < frames; pos += block)
    {
        engine.process (a.data.data() + (size_t) pos * (size_t) a.channels, std::min (block, frames - pos), tel.get());
        for (; seen < tel->framesWritten; ++seen)
        {
            fw.push (tel->frames[seen % rv::kFrameRing]);
            if (++sinceRow >= 23 && fw.ready())
            {
                sinceRow = 0;
                const auto f = fw.compute();
                std::fprintf (out, "%s,%s,%.4f,%.4f,%.3f,%.3f,%.4f\n", argv[1], argv[2], f.pauses, f.syllable, f.bass, f.air, f.flux);
            }
        }
    }
    std::fclose (out);
    return 0;
}
