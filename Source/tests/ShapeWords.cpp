// Which drawing every line of a .lrc gets (ClipLine::shapeFor), and how many lines get one.
// shapewords.exe file.lrc
#include "../app/ClipLine.h"
#include <cstdio>
#include <fstream>
#include <string>

int main (int argc, char** argv)
{
    std::ifstream in (argv[1], std::ios::binary);
    std::string line; int lines = 0, drawn = 0;
    while (std::getline (in, line))
    {
        if (line.size() >= 3 && (unsigned char) line[0] == 0xEF) line = line.substr (3);
        const auto close = line.find (']');
        std::string text = close == std::string::npos ? line : line.substr (close + 1);
        while (! text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
        if (text.empty()) continue;
        std::string low = text; for (auto& c : low) if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        const int k = rv::ui::ClipLine::shapeFor (low);
        ++lines; if (k >= 0) ++drawn;
        std::printf ("%-10s %s\n", k >= 0 ? rv::ui::kClipShapes[k].name : "-", text.c_str());
    }
    std::printf ("== %d of %d lines get a drawing\n", drawn, lines);
}
