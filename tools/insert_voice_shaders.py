"""Puts tools/voice_shaders.txt into Source/app/NebulaGl.h (replacing the earlier copy of the same block); the Wave block, which follows it, is left alone."""
import os
here = os.path.dirname(os.path.abspath(__file__))
P = os.path.join(here, "..", "Source", "app", "NebulaGl.h")
raw = open(P, "rb").read().decode("utf-8"); crlf = "\r\n" in raw; t = raw.replace("\r\n", "\n")
block = open(os.path.join(here, "voice_shaders.txt"), encoding="utf-8").read().replace("\r\n", "\n")
a = t.find('    // ---- "Voice"')
b = t.find('    // ---- "Wave":')
if b < 0: b = t.find("    static juce::String vertexSource()")
assert 0 < a < b
t = t[:a] + block + t[b:]
open(P, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8")); print("voice shaders replaced")
