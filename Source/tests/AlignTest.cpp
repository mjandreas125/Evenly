// LyricAligner against a recorded song: a file of "position logEnergy" (50 ms of the singing band each) and lyrics files.
// Prints the shift found for each lyrics file. Build: C:\dev\voicetest\align.cmd
#include "../app/LyricAligner.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static std::vector<std::pair<double, double>> spansOf (const char* path)
{
    std::ifstream f (path);
    std::vector<std::pair<double, std::string>> L;
    std::string line;
    while (std::getline (f, line))
    {
        if (line.rfind ("\xEF\xBB\xBF", 0) == 0) line = line.substr (3);     // a byte order mark before the first line
        int mm = 0; double ss = 0; char close = 0;
        if (line.size() > 3 && line[0] == '[' && std::sscanf (line.c_str(), "[%d:%lf%c", &mm, &ss, &close) == 3 && close == ']')
            L.push_back ({ mm * 60 + ss, line.substr (line.find (']') + 1) });
    }
    std::vector<std::pair<double, double>> out;
    for (size_t i = 0; i < L.size(); ++i)
    {
        std::istringstream ws (L[i].second); std::string w; int n = 0;
        while (ws >> w) ++n;
        if (n == 0) continue;
        const double nxt = i + 1 < L.size() ? L[i + 1].first : L[i].first + 5.0;
        out.push_back ({ L[i].first, L[i].first + std::min (nxt - L[i].first - 0.1, 0.33 * n + 0.6) });
    }
    return out;
}

int main (int argc, char** argv)
{
    // --lead S --near S: the bounds the app takes from the two lengths (default: the widest, as for a cover)
    double lead = rv::ui::LyricAligner::kMaxLead, nearS = 6.0;
    int first = 2;
    while (first + 1 < argc && argv[first][0] == '-')
    {
        if (std::string (argv[first]) == "--lead") lead = std::atof (argv[first + 1]);
        else if (std::string (argv[first]) == "--near") nearS = std::atof (argv[first + 1]);
        first += 2;
    }
    if (argc < 3 || first >= argc) { std::printf ("AlignTest sound.txt [--lead S] [--near S] a.lrc [b.lrc ...]\n"); return 1; }
    for (int k = first; k < argc; ++k)
    {
        rv::ui::LyricAligner al;
        const auto sp = spansOf (argv[k]);
        std::printf ("%s: %d lines, first %.2f-%.2f\n", argv[k], (int) sp.size(), sp.empty() ? 0.0 : sp[0].first, sp.empty() ? 0.0 : sp[0].second);
        al.reset (sp, 270.0, lead, nearS);
        std::ifstream f (argv[1]);
        double pos; float v; double nextUpdate = 5.0;
        while (f >> pos >> v)
        {
            al.add (pos, v);
            if (pos >= nextUpdate) { if (! al.update()) std::printf ("  %6.1f s: best %.3f at %+.2f, second %.3f | wide %.3f at %+.2f second %.3f near %.3f\n", pos, al.dbgBest, al.dbgOff, al.dbgSecond, al.wBest, al.wOff, al.wSecond, al.wNear); else std::printf ("  %6.1f s: shift %+.2f (quality %.2f)\n", pos, al.shift(), al.quality()); nextUpdate += 5.0; }
        }
        std::printf ("%s -> %s %+.2f s\n", argv[k], al.ready() ? "shift" : "no answer", al.shift());
    }
    return 0;
}
