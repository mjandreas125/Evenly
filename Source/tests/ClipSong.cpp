// A whole song through the Clip picture, for comparing with the video frame by frame (no sound: a made-up beat at the song's tempo
// and syllables while a line is sung). The words, the chorus marks and the plan are made as the app makes them.
// clipsong.exe song.lrc out_frames.txt bpm durationSec t1 t2 t3 ...   (frames at those seconds)
#include "../app/ClipLine.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace rv::ui;

int main (int argc, char** argv)
{
    std::ifstream lf (argv[1]);
    FILE* fo = std::fopen (argv[2], "w");
    const double bpm = std::atof (argv[3]), dur = std::atof (argv[4]);
    std::vector<double> want;
    for (int a = 5; a < argc; ++a) want.push_back (std::atof (argv[a]));
    std::vector<double> ts; std::vector<std::string> tx;
    std::string row;
    while (std::getline (lf, row))
    {
        if (row.size() > 3 && (unsigned char) row[0] == 0xEF) row = row.substr (3);
        if (row.size() < 10 || row[0] != '[') continue;
        ts.push_back (std::atoi (row.substr (1, 2).c_str()) * 60.0 + std::atof (row.substr (4, 5).c_str()));
        std::string s = row.substr (row.find (']') + 1);
        while (! s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        tx.push_back (s);
    }
    // the chorus marks, as Lyrics::markChoruses makes them
    auto norm = [] (const std::string& s) { std::string o; for (unsigned char c : s) if (std::isalnum (c) || c == ' ' || c >= 0x80) o += (char) c; return o; };
    auto words = [] (const std::string& s) { int n = 0; bool in = false; for (char c : s) { if (c != ' ' && ! in) ++n; in = c != ' '; } return n; };
    std::map<std::string, int> seen;
    for (auto& s : tx) if (! s.empty() && words (s) >= 3) ++seen[norm (s)];
    std::vector<char> ch (tx.size(), 0);
    for (size_t q = 0; q < tx.size(); ++q) ch[q] = ! tx[q].empty() && seen.count (norm (tx[q])) && seen[norm (tx[q])] >= 2;
    for (size_t q = 1; q + 1 < tx.size(); ++q) if (! ch[q] && ! tx[q].empty() && ch[q - 1] && ch[q + 1]) ch[q] = 1;
    const auto plan = ClipLine::planSong (tx, ch, ts);
    std::vector<std::pair<int, int>> shapes; for (auto& s : tx) shapes.push_back (ClipLine::shapesFor (s));
    int motif = -1; { std::map<int, float> v; for (size_t q = 0; q < tx.size(); ++q) if (shapes[q].first >= 0) v[shapes[q].first] += ch[q] ? 2.0f : 1.0f; float top = 1.5f; for (auto& [k, x] : v) if (x > top) { top = x; motif = k; } }
    for (size_t q = 0; q < tx.size(); ++q)
        std::printf ("%7.2f act %d %s%s%s%s%s %-9s %s\n", ts[q], plan.act[q], ch[q] ? "C" : " ", plan.stave[q] ? "S" : " ", plan.hue[q] >= 0 ? "H" : " ", plan.world[q] ? "W" : " ", plan.burst[q] ? "B" : " ",
                     shapes[q].first >= 0 ? kClipShapes[shapes[q].first].name : "-", tx[q].c_str());

    ClipLine cl; ClipLine::Output o;
    const double dt = 1.0 / 60.0, period = 60.0 / bpm;
    int seenLine = -1; size_t wi = 0; double nextSyl = 0;
    for (double now = 0; now < dur && wi < want.size(); now += dt)
    {
        ClipInput in; in.dt = (float) dt; in.aspect = 16.0f / 9.0f;
        const double ph = std::fmod (now, period) / period;
        in.beatPeriod = (float) period; in.beatConf = 0.9f; in.beatPulse = (float) std::pow (0.5 + 0.5 * std::cos (6.2831853 * ph), 3.0); in.bright = 0.5f;
        in.level = now > 1.0 ? 0.55f : 0.0f;
        int li = -1; for (size_t q = 0; q < ts.size(); ++q) if (ts[q] <= now) li = (int) q;
        in.haveWords = true; in.storyOpen = true; in.motif = motif;
        if (li >= 0)
        {
            const double nextT = li + 1 < (int) ts.size() ? ts[(size_t) li + 1] : ts[(size_t) li] + 5.0;
            const int w = words (tx[(size_t) li]);
            const double singEnd = ts[(size_t) li] + std::min (nextT - ts[(size_t) li] - 0.1, 0.33 * w + 0.6);
            const bool hasText = ! tx[(size_t) li].empty();
            in.singing = hasText && now < singEnd;
            in.lineStarted = hasText && li != seenLine;
            in.chorus = ch[(size_t) li]; in.backing = hasText && tx[(size_t) li][0] == '(';
            in.lineLeft = (float) (nextT - now);
            in.lineShape = shapes[(size_t) li].first; in.lineShape2 = shapes[(size_t) li].second;
            in.act = plan.act[(size_t) li]; in.stave = plan.stave[(size_t) li] != 0; in.staveUp = plan.stave[(size_t) li] == 2; in.hue = plan.hue[(size_t) li]; in.world = plan.world[(size_t) li] != 0; in.burst = plan.burst[(size_t) li] != 0;
            if (in.backing)
            {
                const auto close = tx[(size_t) li].find (')');
                const double frac = close == std::string::npos ? 1.0 : (double) words (tx[(size_t) li].substr (0, close)) / std::max (1, w);
                in.backingNow = in.singing && now < ts[(size_t) li] + frac * (singEnd - ts[(size_t) li]) + 0.15;
            }
        }
        seenLine = li;
        for (size_t q = (size_t) std::max (0, li + 1); q < ts.size(); ++q) if (! tx[q].empty()) { in.nextLineIn = (float) (ts[q] - now); break; }
        // sounds: the beat, and a syllable every ~0.3 s while singing
        const bool beat = std::fmod (now, period) < dt;
        if (in.singing && now >= nextSyl) { in.onset = true; in.onsetStrength = 0.6f; in.onsetHigh = 0.5f; nextSyl = now + 0.3; }
        else if (beat) { in.onset = true; in.onsetStrength = 0.8f; in.onsetHigh = 0.3f; }
        in.voiced = in.singing ? 0.8f : 0.3f; in.intensity = 0.6f; in.reg = 0.5f;
        if (dur > 60.0 && now > dur - 4.5) in.forceLetters = "AM"; else if (dur > 60.0 && now > dur - 10.0) in.forceShape = ClipLine::shapeNamed ("flag");
        cl.step (in, o);
        if (now >= want[wi])
        {
            ++wi;
            std::fprintf (fo, "frame %.2f %.3f %.3f %.3f %d\n", now, o.bg[0], o.bg[1], o.bg[2], o.ink ? 1 : 0);
            for (int L = 0; L < o.lines; ++L)
            {
                if (o.alpha[L] < 0.01f) continue;
                std::fprintf (fo, "line %.3f %.3f %.3f %.3f", o.col[L][0], o.col[L][1], o.col[L][2], o.alpha[L]);
                for (int i = 0; i < ClipLine::N; i += 2) std::fprintf (fo, " %.4f %.4f", o.pts[L][i * 2], o.pts[L][i * 2 + 1]);
                std::fprintf (fo, "\n");
            }
        }
    }
    std::fclose (fo);
    return 0;
}
