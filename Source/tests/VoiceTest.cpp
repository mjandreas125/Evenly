// Checks the voice analyser with made-up singing: a held note, a glide, vibrato, a pause, a falling phrase, with and without
// a band behind it (bass, a chord, hi-hat noise). Prints the pitch error and what the features say.
#include "../app/VoiceDsp.h"
#include <cstdio>
#include <cstdlib>
#include <random>

static double midiOf (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

// pitch contour of the made-up phrase (Hz, 0 = silence) at time t
static double contour (double t)
{
    if (t < 1.0) return 0;
    if (t < 2.5) return 220.0;                                          // held note
    if (t < 3.5) return 220.0 * std::pow (1.5, t - 2.5);                // glide up a fifth
    if (t < 5.0) return 330.0 * std::pow (2.0, 0.4 * std::sin (2 * 3.14159265 * 5.5 * t) / 12.0);   // vibrato (+-0.4 semitone)
    if (t < 5.6) return 0;                                              // pause
    if (t < 7.0) return 330.0 * std::pow (196.0 / 330.0, (t - 5.6) / 1.4);   // falls to G3
    return 0;
}

static int syllableTest (bool band)
{
    const double fs = 48000; rv::VoiceDsp d; d.prepare (fs);
    std::mt19937 rng (3); std::normal_distribution<float> nd (0, 1);
    std::vector<float> buf; double ph = 0; uint32_t seen = 0; int events = 0; std::vector<double> evT;
    const double beat = 0.25;                         // a syllable every 250 ms, from 0.5 s: 12 of them
    for (int i = 0; i < (int) (4.0 * fs); ++i)
    {
        const double t = i / fs; double s = 0;
        const double k = std::fmod (t - 0.5, beat);
        if (t >= 0.5 && t < 3.5)
        {
            const int idx = (int) ((t - 0.5) / beat);
            const bool hissy = idx % 3 == 2;          // every third one is an "s" / "t": noise, not a vowel
            if (hissy) { if (k < 0.07) s += 0.5 * nd (rng) * std::exp (-k * 18.0); }
            else if (k < 0.15) { ph += 2 * 3.14159265 * (200.0 + 20 * (idx % 4)) / fs; for (int h = 1; h <= 10; ++h) s += std::sin (h * ph) * std::exp (-0.25 * h) * 0.3 * std::exp (-k * 8.0); }
        }
        if (band) s += 0.30 * std::sin (2 * 3.14159265 * 55.0 * t) + 0.06 * nd (rng);
        buf.push_back ((float) s);
        if (buf.size() == 480)
        {
            d.push (buf.data(), 480); buf.clear();
            const auto& f = d.features();
            if (f.onsetSeq != seen) { events += (int) (f.onsetSeq - seen); evT.push_back (t); seen = f.onsetSeq; }
        }
    }
    std::printf ("sharp sounds: 12 syllables made, %d found (band: %s)\n", events, band ? "yes" : "no");
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2) return syllableTest (std::atoi (argv[2]) != 0);
    const double fs = 48000;
    const bool band = argc > 1 && std::atoi (argv[1]) != 0;
    rv::VoiceDsp d; d.prepare (fs);
    std::mt19937 rng (1); std::normal_distribution<float> nd (0, 1);
    const int total = (int) (8.0 * fs);
    std::vector<float> buf;
    double ph = 0, hat = 0;
    int frames = 0, n = 0; double errSum = 0; int errN = 0, voicedWrong = 0, silentWrong = 0, silentN = 0, voicedN = 0;
    float maxHold = 0, maxVib = 0, vibInVibrato = 0; int vibN = 0; float slopeGlide = 0; int slopeN = 0, slopeFall = 0, fallN = 0;
    float endAt = 0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / fs;
        const double hz = contour (t);
        double s = 0;
        if (hz > 0)
        {
            ph += 2 * 3.14159265 * hz / fs;
            for (int h = 1; h <= 14; ++h) s += std::sin (h * ph) * std::exp (-0.22 * h) * (h == 3 ? 1.6 : 1.0) * 0.30;   // a vowel-ish spectrum
            s *= 0.5 * (1 - std::cos (std::min (1.0, std::fmod (t, 1.0) * 40) * 3.14159));   // soft start of a sound
        }
        if (band)
        {
            s += 0.40 * std::sin (2 * 3.14159265 * 55.0 * t) + 0.25 * std::sin (2 * 3.14159265 * 110.0 * t);                 // bass
            s += 0.10 * (std::sin (2 * 3.14159265 * 261.6 * t) + std::sin (2 * 3.14159265 * 329.6 * t) + std::sin (2 * 3.14159265 * 392.0 * t)); // a chord
            hat = std::fmod (t, 0.25) < 0.05 ? 1.0 : 0.0;
            s += hat * 0.12 * nd (rng);                                                                                      // hi-hat noise
            if (std::fmod (t, 0.5) < 0.03) s += 0.5 * std::sin (2 * 3.14159265 * 60.0 * t) * 1.0;                            // kick
        }
        buf.push_back ((float) s);
        if (buf.size() == 480)
        {
            const int made = d.push (buf.data(), 480);
            buf.clear();
            for (int k = 0; k < made; ++k)
            {
                const auto& f = d.features();
                const double tt = t;
                const double truth = contour (tt - 0.03);
                ++frames;
                if (std::getenv ("VDBG") && frames % 5 == 0 && tt > 1.0 && tt < 2.5) std::printf ("t=%.2f truth=%.0f voiced=%.2f midi=%.1f | minv=%.3f rel=%.3f rms=%.4f best=%d held=%.2f last=%.2f slow=%.2f hold=%.2f\n", tt, truth, f.voiced, f.midi, d.dbgMin, d.dbgRel, d.dbgRms, d.dbgBest, d.dbgHeld, d.dbgLast, d.dbgSlow, f.hold);
                if (truth > 0 && tt > 1.1)
                {
                    ++voicedN;
                    if (f.voiced < 0.3) ++voicedWrong;
                    else { errSum += std::abs (f.midi - midiOf (truth)); ++errN; }
                    if (tt > 2.0 && tt < 2.5) maxHold = std::max (maxHold, f.hold);
                    if (tt > 3.9 && tt < 5.0) { vibInVibrato += f.vibrato; ++vibN; }
                    if (tt > 2.7 && tt < 3.4) { slopeGlide += f.slope; ++slopeN; }
                    if (tt > 6.0 && tt < 6.9) { slopeFall += f.slope; ++fallN; }
                }
                else if (truth == 0 && tt > 0.3) { ++silentN; if (f.voiced > 0.3 && tt > 0.45 && !(tt > 5.0 && tt < 5.15) && !(tt > 7.0 && tt < 7.15)) ++silentWrong; }
                if (tt > 7.0 && f.endPulse > 0.9) endAt = (float) tt;
            }
        }
        ++n;
    }
    std::printf ("band behind the voice: %s\n", band ? "yes (bass, chord, hi-hat, kick)" : "no");
    std::printf ("pitch error while it sings: mean %.2f semitones over %d frames | voice missed in %d of %d frames (%.0f%%)\n",
                 errN ? errSum / errN : -1.0, errN, voicedWrong, voicedN, voicedN ? 100.0 * voicedWrong / voicedN : 0.0);
    std::printf ("voice claimed in silence (frames): %d of %d\n", silentWrong, silentN);
    std::printf ("held note: hold reaches %.2f (want >0.6) | vibrato section: mean vibrato %.2f (want >0.3) | glide up: slope %.1f st/s (true 7.0) | fall: slope %.1f st/s (true -%.1f)\n",
                 maxHold, vibN ? vibInVibrato / vibN : 0.0f, slopeN ? slopeGlide / slopeN : 0.0f, fallN ? slopeFall / fallN : 0.0f, 12.0 * std::log2 (330.0 / 196.0) / 1.4);
    std::printf ("phrase end noticed at %.2f s (true 7.0)\n", endAt);
    return 0;
}
