// What happens to the level when the sound drops out and comes back?
// Speech-like noise at a fixed level, a pause of `gap` seconds, the same speech again.
// Prints the output level (RMS, dBFS) of the last second before the pause and of the first
// moments after it, plus the worst overshoot and undershoot against the level the leveler reached.
//   rovno_pausetest [--boost 0|1] [--loudness leveler|fxsound] [--noise dBFS]
#include "../dsp/Engine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr double kFs = 48000;
constexpr double kPi = 3.14159265358979323846;

struct Speech
{
    explicit Speech (double levelDb) : amp (std::pow (10.0, levelDb / 20.0)) {}
    float next (double t)
    {
        // 300 Hz..3 kHz noise, chopped into syllables at ~3.7 Hz.
        const double w = rng (gen);
        hp += 0.0393 * (w - hp);              // ~300 Hz low-pass state, subtract to high-pass
        const double bp = (w - hp);
        lp += 0.3 * (bp - lp);                // ~3 kHz low-pass
        const double env = 0.12 + 0.88 * std::pow (std::abs (std::sin (2 * kPi * 3.7 * t)), 1.5);
        return (float) (lp * env * amp * 2.2);
    }
    double amp, hp = 0, lp = 0;
    std::mt19937 gen { 7 };
    std::normal_distribution<double> rng { 0.0, 0.35 };
};

double rmsDb (const std::vector<float>& x, size_t from, size_t to)   // interleaved stereo, frames
{
    double s = 0;
    for (size_t i = from; i < to && i < x.size() / 2; ++i) s += (double) x[2 * i] * x[2 * i];
    return 10 * std::log10 (std::max (1e-12, s / std::max<size_t> (1, to - from)));
}
}

int main (int argc, char** argv)
{
    int boost = 0, fxsound = 0;
    double noiseDb = -200, afterDb = 0;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string k = argv[i];
        if (k == "--boost") boost = std::atoi (argv[i + 1]);
        else if (k == "--loudness") fxsound = std::string (argv[i + 1]) == "fxsound";
        else if (k == "--noise") noiseDb = std::atof (argv[i + 1]);
        else if (k == "--after") afterDb = std::atof (argv[i + 1]);   // the sound that returns is this much louder (an advert)
    }
    const double levels[] = { -30.0, -16.0, -10.0 };      // RMS of the source, dBFS: very quiet old video (gain pegged), normal, hot
    const double gaps[] = { 0.3, 0.8, 1.5, 3.0, 6.0, 15.0 };
    std::printf ("loudness=%s boost=%d room noise=%.0f dBFS\n", fxsound ? "fxsound" : "leveler", boost, noiseDb);
    std::printf ("%8s %6s | %7s | %7s %7s %7s %7s %7s | %s\n", "src dBFS", "gap s", "before", "0-.5s", ".5-1s", "1-1.6s", "2-2.5s", "4-4.5s", "max jump after resume (dB per 0.1 s)");
    for (double lv : levels)
        for (double gap : gaps)
        {
            rv::Engine e;
            e.prepare (kFs, 2);
            rv::EngineParams p;
            p.boost = boost ? 64 : 0;
            if (fxsound) p.loudness = (uint32_t) rv::LoudnessMode::FxSoundBoost;
            e.setParams (p);
            Speech sp (lv);
            std::mt19937 ng (3);
            std::normal_distribution<double> nd (0.0, std::pow (10.0, noiseDb / 20.0));
            const double lead = 20.0, tail = 6.0;
            const size_t n = (size_t) ((lead + gap + tail) * kFs);
            std::vector<float> x (n * 2);
            for (size_t i = 0; i < n; ++i)
            {
                const double t = i / kFs;
                const bool speaking = t < lead || t >= lead + gap;
                float v = speaking ? sp.next (t) * (t >= lead ? (float) std::pow (10.0, afterDb / 20.0) : 1.0f) : 0.0f;
                if (noiseDb > -150) v += (float) nd (ng);
                x[2 * i] = x[2 * i + 1] = v;
            }
            auto tel = std::make_unique<rv::Telemetry>();
            std::memset (tel.get(), 0, sizeof (rv::Telemetry));
            std::vector<float> gain (n / 480 + 1);
            for (size_t pos = 0; pos < n; pos += 480)
            {
                e.process (x.data() + 2 * pos, (int) std::min<size_t> (480, n - pos), tel.get());
                gain[pos / 480] = tel->levelerGainDb;
            }
            auto g = [&] (double t) { return (double) gain[std::min (gain.size() - 1, (size_t) (t * kFs / 480))]; };
            const size_t resume = (size_t) ((lead + gap) * kFs);
            auto at = [&] (double s0, double s1) { return rmsDb (x, resume + (size_t) (s0 * kFs), resume + (size_t) (s1 * kFs)); };
            const double before = rmsDb (x, (size_t) ((lead - 1.08) * kFs), (size_t) (lead * kFs));
            // biggest rise between consecutive 100 ms windows after resuming
            double worst = 0, prev = at (0, 0.54);
            for (double t = 0.1; t < 5.0; t += 0.1) { const double c = at (t, t + 0.54); worst = std::max (worst, std::abs (c - prev)); prev = c; }
            double hot = 0, hotMax = -200;
            for (double t = 0; t < 4.0; t += 0.05) { const double c = at (t, t + 0.1); hotMax = std::max (hotMax, c - before); if (c > before + 6) hot += 0.05; }
            std::printf ("%8.0f %6.1f | %7.1f | %7.1f %7.1f %7.1f %7.1f %7.1f | %5.1f\n", lv, gap, before, at (0, 0.54), at (0.54, 1.08), at (1.08, 1.62), at (2.0, 2.54), at (4.0, 4.54), worst);
            if (afterDb > 0) std::printf ("%8s %6s | loudest 0.1 s is %+.1f dB over the level before; more than 6 dB over for %.2f s\n", "", "", hotMax, hot);
            std::printf ("%8s %6s | gain %5.1f | %7.1f %7.1f %7.1f %7.1f %7.1f |\n", "", "", g (lead - 0.01), g (lead + gap + 0.1), g (lead + gap + 0.5), g (lead + gap + 1.0), g (lead + gap + 2.0), g (lead + gap + 4.0));
        }
    return 0;
}
