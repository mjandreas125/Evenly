// Live check of the Clip picture without the app: listens to what the computer is playing (loopback, makes no sound), feeds the same
// ClipLine the app uses, and prints once a second what came in and how far the line moved.
// cliplive.exe [seconds]
#include "../app/VoiceCapture.h"
#include "../app/ClipLine.h"
#include <cstdio>
#include <cstdlib>

int main (int argc, char** argv)
{
    const int secs = argc > 1 ? std::atoi (argv[1]) : 15;
    auto& cap = rv::VoiceCapture::get();
    rv::ui::ClipLine line;
    rv::ui::ClipLine::Output o;
    uint32_t seq = 0; std::vector<std::pair<double, float>> band; float bandMax = -99;
    int onsets = 0, frames = 0; float voicedSum = 0, intSum = 0, strMax = 0, maxD = 0;
    for (int f = 0; f < secs * 60; ++f)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (16));
        const auto vf = cap.features();
        rv::ui::ClipInput in;
        in.dt = 1.0f / 60.0f; in.aspect = 2.33f;
        if (vf.onsetSeq != seq) { in.onset = seq != 0; seq = vf.onsetSeq; in.onsetStrength = vf.onsetStrength; in.onsetHigh = vf.onsetHigh; if (in.onset) { ++onsets; strMax = std::max (strMax, vf.onsetStrength); } }
        in.voiced = vf.voiced; in.intensity = vf.intensity; in.hold = vf.hold; in.reg = vf.reg; in.level = 0.5f;
        cap.takeSingingBand (band); for (auto& b : band) bandMax = std::max (bandMax, b.second);
        line.step (in, o);
        for (int i = 0; i < rv::ui::ClipLine::N; ++i) maxD = std::max (maxD, std::abs (o.pts[0][i * 2 + 1]));
        voicedSum += vf.voiced; intSum += vf.intensity; ++frames;
        if (frames == 60)
        {
            std::printf ("live %d sinceSound %.1fs | onsets %d (max str %.2f) | voiced %.2f int %.2f | line moved up to %.3f | loudest 1-4k block log %.1f\n",
                         cap.listening() ? 1 : 0, cap.secondsSinceSound(), onsets, strMax, voicedSum / frames, intSum / frames, maxD, bandMax);
            std::fflush (stdout);
            onsets = 0; frames = 0; voicedSum = intSum = strMax = maxD = 0; bandMax = -99;
        }
    }
    std::_Exit (0);
}
