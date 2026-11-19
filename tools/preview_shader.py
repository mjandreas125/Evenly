"""Renders tools/crystal.frag offscreen (the same GLSL the app compiles) to a PNG, so a new picture can be checked without the app.
usage: python preview_shader.py out.png [width]   (21:9 by default)
"""
import sys, os
import numpy as np
import moderngl
from PIL import Image

K = 26
VERT = "#version 330 core\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n"
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, sys.argv[3] if len(sys.argv) > 3 else "crystal.frag"), encoding="utf-8").read()
FRAG = "#version 330 core\n#define NB %d\n" % K + src

W = int(sys.argv[2]) if len(sys.argv) > 2 else 1400
H = int(W * 9 / 21)
ctx = moderngl.create_standalone_context(require=330)
prog = ctx.program(vertex_shader=VERT, fragment_shader=FRAG)       # raises with the compiler log if the GLSL is wrong
vao = ctx.vertex_array(prog, [])
fbo = ctx.framebuffer(ctx.texture((W, H), 4))


def bands(kick, hat, swell):
    x = np.arange(K, dtype=np.float32)
    v = 0.18 + 0.45 * np.exp(-((x - 6) / 7) ** 2) + 0.35 * kick * (x < 8) + 0.25 * swell * np.exp(-((x - 13) / 3) ** 2) + 0.30 * hat * (x >= 17)
    return np.clip(v, 0, 1).astype(np.float32)


def render(name, **u):
    d = dict(uRes=(W, H), uTime=3.0, uPhase=2.0, uRot=0.4, uBass=0.2, uMid=0.2, uTreb=0.1, uBeat=0.0, uLevel=0.3, uIdle=0.0,
             uA=(0.4, 0.6, 1.0), uB=(0.7, 0.3, 1.0), uC=(1.0, 0.5, 0.6))
    d.update({k: v for k, v in u.items() if not k.startswith("_")})
    b = u.get("_bands", bands(0.0, 0.0, 0.5))
    tex = ctx.texture((K, 1), 1, b.tobytes(), dtype="f4"); tex.filter = (moderngl.LINEAR, moderngl.LINEAR); tex.use(0)
    for k, v in d.items():
        if k in prog:
            prog[k].value = v
    if "uBandTex" in prog:
        prog["uBandTex"].value = 0
    fbo.use(); fbo.clear(0, 0, 0, 1); vao.render(moderngl.TRIANGLES, vertices=3)
    img = Image.frombytes("RGBA", (W, H), fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert("RGB")
    tex.release()
    return img


frames = [
    render("calm", uBass=0.15, uMid=0.2, uTreb=0.1, uBeat=0.0, uLevel=0.25, uPhase=1.0, uRot=0.2, _bands=bands(0, 0.2, 0.3)),
    render("kick", uBass=0.9, uMid=0.5, uTreb=0.3, uBeat=1.0, uLevel=0.8, uPhase=6.0, uRot=1.1, _bands=bands(1.0, 0.3, 0.6)),
    render("full", uBass=0.55, uMid=0.7, uTreb=0.8, uBeat=0.35, uLevel=0.6, uPhase=11.0, uRot=2.3, _bands=bands(0.5, 1.0, 1.0)),
]
sheet = Image.new("RGB", (W, H * len(frames) + 6 * (len(frames) - 1)), (40, 40, 40))
for i, f in enumerate(frames):
    sheet.paste(f, (0, i * (H + 6)))
sheet.save(sys.argv[1])
print("rendered", sheet.size)
