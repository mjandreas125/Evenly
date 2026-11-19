// Runs ClipLine on a file of per-frame inputs and writes the lines at the asked moments, for tools/clip_compare.py.
// input lines: t onset strength high voiced intensity hold reg level haveWords lineStarted singing chorus backing lineLeft shape storyOpen aspect
// Build: C:\dev\voicetest\clip.cmd
#include "../app/ClipLine.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("ClipTest inputs.txt times.txt out.txt\n"); return 1; }
    std::set<long> want;
    { std::ifstream f (argv[2]); double x; while (f >> x) want.insert ((long) std::lround (x * 100)); }
    std::ifstream in (argv[1]);
    std::FILE* out = std::fopen (argv[3], "w");
    rv::ui::ClipLine cl;
    cl.reset();
    rv::ui::ClipLine::Output o;
    std::string line;
    double prevT = 0;
    while (std::getline (in, line))
    {
        std::istringstream s (line);
        double t; int onset, haveWords, started, singing, chorus, backing, shape, story;
        rv::ui::ClipInput ci;
        s >> t >> onset >> ci.onsetStrength >> ci.onsetHigh >> ci.voiced >> ci.intensity >> ci.hold >> ci.reg >> ci.level >> haveWords >> started >> singing >> chorus >> backing >> ci.lineLeft >> shape >> story >> ci.aspect >> ci.nextLineIn;
        ci.dt = (float) (t - prevT); prevT = t;
        ci.onset = onset != 0; ci.haveWords = haveWords != 0; ci.lineStarted = started != 0; ci.singing = singing != 0;
        ci.chorus = chorus != 0; ci.backing = backing != 0; ci.lineShape = shape; ci.storyOpen = story != 0;
        cl.step (ci, o);
        // the frame nearest to an asked moment (inputs come at 60 per second)
        for (long k = (long) std::lround (t * 100) - 1; k <= (long) std::lround (t * 100); ++k)
            if (want.count (k))
            {
                want.erase (k);
                std::fprintf (out, "frame %.2f %d\n", t, o.lines);
                for (int L = 0; L < o.lines; ++L)
                {
                    std::fprintf (out, "line %.2f %.2f %.2f %.3f", o.col[L][0], o.col[L][1], o.col[L][2], o.alpha[L]);
                    for (int i = 0; i < rv::ui::ClipLine::N; i += 2) std::fprintf (out, " %.4f %.4f", o.pts[L][i * 2], o.pts[L][i * 2 + 1]);
                    std::fprintf (out, "\n");
                }
            }
    }
    std::fclose (out);
    return 0;
}
