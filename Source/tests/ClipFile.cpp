// The Clip picture fed from a wav file instead of the speakers (nothing is played): the 26 third-octave levels (like the effect's
// analyser) -> ClipOnsets, and VoiceDsp -> ClipLine; a line a second.
// clipfile.exe file.wav [startSec]
#include "../app/VoiceDsp.h"
#include "../app/ClipLine.h"
#include <complex>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>

static void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = -6.2831853f / (float) len; const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<float> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

int main (int argc, char** argv)
{
    std::ifstream fin (argv[1], std::ios::binary);
    std::vector<char> raw ((std::istreambuf_iterator<char> (fin)), {});
    size_t p = 12; int ch = 2, fs = 48000, bits = 16; const char* data = nullptr; size_t len = 0;
    while (p + 8 <= raw.size())
    {
        const uint32_t sz = *(const uint32_t*) &raw[p + 4];
        if (! std::memcmp (&raw[p], "fmt ", 4)) { ch = *(const int16_t*) &raw[p + 10]; fs = *(const int32_t*) &raw[p + 12]; bits = *(const int16_t*) &raw[p + 22]; }
        if (! std::memcmp (&raw[p], "data", 4)) { data = &raw[p + 8]; len = std::min<size_t> (sz, raw.size() - p - 8); break; }
        p += 8 + sz + (sz & 1);
    }
    if (data == nullptr || bits != 16) { std::printf ("need 16-bit wav (bits %d)\n", bits); return 1; }
    const size_t frames = len / (2 * ch);
    std::vector<float> all (frames);
    for (size_t i = 0; i < frames; ++i) { float s = 0; for (int c = 0; c < std::min (ch, 2); ++c) s += ((const int16_t*) data)[i * ch + c] / 32768.0f; all[i] = s / std::min (ch, 2); }

    std::vector<std::pair<double, std::string>> lrc;
    if (argc > 2)
    {
        std::ifstream lf (argv[2]); std::string row;
        while (std::getline (lf, row))
        {
            if (row.size() > 3 && (unsigned char) row[0] == 0xEF) row = row.substr (3);
            if (row.size() < 10 || row[0] != '[') continue;
            const double tt = std::atoi (row.substr (1, 2).c_str()) * 60.0 + std::atof (row.substr (4, 5).c_str());
            std::string tx = row.substr (row.find (']') + 1); while (! tx.empty() && (tx.back() == '\r' || tx.back() == ' ')) tx.pop_back();
            for (auto& c : tx) if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
            lrc.push_back ({ tt, tx });
        }
    }
    FILE* fo = argc > 3 ? std::fopen (argv[3], "w") : nullptr; double nextFrame = 18.0; int seenLine = -1;
    rv::VoiceDsp dsp; dsp.prepare (fs); dsp.reset();
    rv::ui::ClipLine line; rv::ui::ClipLine::Output o;
    rv::ui::ClipOnsets ons;
    const int hop = fs / 60, W = 4096;
    std::vector<float> win (W); for (int i = 0; i < W; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * i / (W - 1));
    std::vector<std::complex<float>> buf (W);
    uint32_t seq = 0; int onsets = 0, myOn = 0, n = 0; float vs = 0, tn = 0, smax = 0, maxD = 0; int moving = 0; float shareSum = 0;
    for (size_t f0 = (size_t) W; f0 + (size_t) hop <= frames; f0 += (size_t) hop)
    {
        dsp.push (&all[f0], hop);
        const auto vf = dsp.features();
        // third octaves 50 Hz .. 16 kHz from the newest 4096 samples
        for (int i = 0; i < W; ++i) buf[(size_t) i] = all[f0 + hop - W + i] * win[(size_t) i];
        fft (buf);
        float db[26];
        for (int b = 0; b < 26; ++b)
        {
            const double fc = 50.0 * std::pow (2.0, b / 3.0), f1 = fc / std::pow (2.0, 1.0 / 6.0), f2 = fc * std::pow (2.0, 1.0 / 6.0);
            double e = 0; for (int k = std::max (1, (int) (f1 * W / fs)); k <= (int) (f2 * W / fs) && k < W / 2; ++k) e += std::norm (buf[(size_t) k]);
            db[b] = (float) (10.0 * std::log10 (e + 1e-12) - 20.0 * std::log10 (W / 4.0));
        }
        ons.step (db, 26, 1.0f / 60.0f);
        { double a = 0, m = 0; for (int b = 0; b < 26; ++b) { const double e = std::pow (10.0, db[b] / 10.0); a += e; if (b >= 9 && b < 20) m += e; } shareSum += (float) (m / (a + 1e-12)); }
        rv::ui::ClipInput in; in.dt = 1.0f / 60.0f; in.aspect = 2.33f;
        if (vf.onsetSeq != seq) { seq = vf.onsetSeq; ++onsets; }
        if (ons.onset) { in.onset = true; in.onsetStrength = ons.strength; in.onsetHigh = ons.high; ++myOn; smax = std::max (smax, ons.strength); }
        in.voiced = vf.voiced; in.intensity = vf.intensity; in.hold = vf.hold; in.reg = vf.reg; in.level = 0.5f;
        const double now = (double) f0 / fs;
        in.beatPeriod = 0.5f; in.beatConf = 0.8f; in.beatPulse = std::pow (0.5f + 0.5f * std::cos (6.2831853f * (float) now / 0.5f), 3.0f); in.bright = 0.5f;
        if (! lrc.empty())
        {
            int li = -1; for (size_t q = 0; q < lrc.size(); ++q) if (lrc[q].first <= now) li = (int) q;
            in.haveWords = true; in.storyOpen = true;
            if (li >= 0 && ! lrc[(size_t) li].second.empty())
            {
                const auto sh = rv::ui::ClipLine::shapesFor (lrc[(size_t) li].second);
                in.lineStarted = li != seenLine; in.singing = true; in.lineShape = sh.first; in.lineShape2 = sh.second;
                in.lineLeft = li + 1 < (int) lrc.size() ? (float) (lrc[(size_t) li + 1].first - now) : 4.0f;
                if (in.lineStarted) std::printf ("%6.2fs line: %-12s %-10s %s\n", now, sh.first >= 0 ? rv::ui::kClipShapes[sh.first].name : "-", sh.second >= 0 ? rv::ui::kClipShapes[sh.second].name : "-", lrc[(size_t) li].second.c_str());
            }
            seenLine = li;
            if (argc > 4 && now > 31.0) in.forceLetters = argv[4];   // the ending, to look at
        }
        line.step (in, o);
        if (fo != nullptr && now >= nextFrame && now < 34.0)
        {
            nextFrame += 0.2;
            std::fprintf (fo, "frame %.2f\n", now);
            for (int L = 0; L < o.lines; ++L)
            {
                std::fprintf (fo, "line %.3f %.3f %.3f %.3f", o.col[L][0], o.col[L][1], o.col[L][2], o.alpha[L]);
                for (int i = 0; i < rv::ui::ClipLine::N; i += 2) std::fprintf (fo, " %.4f %.4f", o.pts[L][i * 2], o.pts[L][i * 2 + 1]);
                std::fprintf (fo, "\n");
            }
        }
        float fm = 0; for (int i = 0; i < rv::ui::ClipLine::N; ++i) fm = std::max (fm, std::abs (o.pts[0][i * 2 + 1])); maxD = std::max (maxD, fm); if (fm > 0.04f) ++moving;
        vs += vf.voiced; tn += 0;
        if (++n == 60)
        {
            std::printf ("%4.0fs voice-onsets %2d | new onsets %2d (max %.2f) | voiced %.2f tonal %.2f (mid share %.2f) | line up to %.2f, moving %3d%% of frames\n", (double) f0 / fs, onsets, myOn, smax, vs / n, tn / n, shareSum / n, maxD, moving * 100 / n);
            n = 0; moving = 0; shareSum = 0; onsets = myOn = 0; vs = tn = smax = maxD = 0;
        }
    }
    return 0;
}
