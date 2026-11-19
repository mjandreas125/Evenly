P = r"D:\Rovno\Source\app\NebulaGl.h"
raw = open(P, "rb").read().decode("utf-8"); crlf = "\r\n" in raw; t = raw.replace("\r\n", "\n")
block = open(r"D:\Rovno\tools\wave_shader.txt", encoding="utf-8").read().replace("\r\n", "\n")
marker = "    static juce::String vertexSource()"
a = t.find('    // ---- "Wave":')
b = t.find(marker)
assert b > 0 and t.count(marker) == 1
if a >= 0 and a < b:
    t = t[:a] + block + t[b:]; print("wave shader replaced")
else:
    t = t.replace(marker, block + marker); print("wave shader inserted")
open(P, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
