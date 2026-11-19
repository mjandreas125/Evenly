"""Frames of crystal.frag at different moments of its own life (no music change), to see that it keeps changing.
usage: python series_shader.py out.png [width] [shader]"""
import sys, os
import numpy as np
import moderngl
from PIL import Image

K = 26
VERT = "#version 330 core\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n"
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, sys.argv[3] if len(sys.argv) > 3 else "crystal.frag"), encoding="utf-8").read()
W = int(sys.argv[2]) if len(sys.argv) > 2 else 900
H = int(W * 9 / 21)
ctx = moderngl.create_standalone_context(require=330)
prog = ctx.program(vertex_shader=VERT, fragment_shader="#version 330 core\n#define NB %d\n" % K + src)
vao = ctx.vertex_array(prog, [])
fbo = ctx.framebuffer(ctx.texture((W, H), 4))
bands = np.clip(0.2 + 0.4 * np.exp(-((np.arange(K) - 6) / 7.0) ** 2), 0, 1).astype(np.float32)
tex = ctx.texture((K, 1), 1, bands.tobytes(), dtype="f4"); tex.filter = (moderngl.LINEAR, moderngl.LINEAR); tex.use(0)


def render(tm):
    # the app's clocks as they run in quiet music: flight 0.30/s + a little, rotation 0.05/s
    d = dict(uRes=(W, H), uTime=tm, uPhase=tm * 0.34, uRot=tm * 0.056, uBass=0.2, uMid=0.2, uTreb=0.1, uBeat=0.0, uLevel=0.3, uIdle=0.0,
             uA=(0.35, 0.65, 1.0), uB=(0.75, 0.35, 1.0), uC=(1.0, 0.55, 0.45))
    for k, v in d.items():
        if k in prog: prog[k].value = v
    prog["uBandTex"].value = 0
    fbo.use(); fbo.clear(0, 0, 0, 1); vao.render(moderngl.TRIANGLES, vertices=3)
    return Image.frombytes("RGBA", (W, H), fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert("RGB")


times = [0, 3, 6, 40, 80, 120]
frames = [render(t) for t in times]
a = [np.asarray(f, dtype=np.float32) for f in frames]
print("change between frames 3 s apart: %.1f, %.1f  | 40 s later: %.1f | 80 s later: %.1f" %
      (np.abs(a[1] - a[0]).mean(), np.abs(a[2] - a[1]).mean(), np.abs(a[3] - a[2]).mean(), np.abs(a[4] - a[3]).mean()))
cols = 2
rows = (len(frames) + cols - 1) // cols
sheet = Image.new("RGB", (W * cols + 6 * (cols - 1), H * rows + 6 * (rows - 1)), (40, 40, 40))
for i, f in enumerate(frames):
    sheet.paste(f, ((i % cols) * (W + 6), (i // cols) * (H + 6)))
sheet.save(sys.argv[1]); print("saved", sheet.size)
