"""Copies tools/crystal.frag into NebulaGl.h (the function crystalSource). Run it after editing the shader."""
import os, re

here = os.path.dirname(os.path.abspath(__file__))
header = os.path.join(here, "..", "Source", "app", "NebulaGl.h")
frag = open(os.path.join(here, "crystal.frag"), encoding="utf-8").read().replace("\r\n", "\n")
text = open(header, encoding="utf-8").read()

func = (
    "    // The \"Crystal\" picture: a mirrored, glowing kaleidoscope fractal in three depth layers. Kept in sync with tools/crystal.frag\n"
    "    // by tools/embed_crystal.py; tools/preview_shader.py renders that file to a PNG (and compiles it) without starting the app.\n"
    "    static juce::String crystalSource()\n"
    "    {\n"
    "        return juce::String (\"#version 330 core\\n#define NB \") + juce::String (kBands) + R\"GLSL(\n"
    + frag +
    ")GLSL\";\n"
    "    }\n\n"
)

# the function ends at its own raw-string terminator; a plain "end of block" match would stop inside the shader text
pat = re.compile(r"    // The \"Crystal\" picture:.*?\n    static juce::String crystalSource\(\)\n    \{\n.*?\)GLSL\";\n    \}\n\n", re.S)
if pat.search(text):
    text = pat.sub(lambda m: func, text, count=1)
    print("replaced existing crystalSource")
else:
    marker = "    static juce::String vertexSource()"
    assert text.count(marker) == 1, "marker not found"
    text = text.replace(marker, func + marker)
    print("inserted crystalSource")
open(header, "w", encoding="utf-8", newline="\n").write(text)
