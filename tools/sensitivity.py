"""How much does the picture change from ONE frame to the next when the music jumps (a kick), and when time just moves on?
Compares shader files. usage: python sensitivity.py crystal_v1.frag crystal.frag"""
import sys, os
import numpy as np
import moderngl

K = 26
VERT = "#version 330 core\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n"
here = os.path.dirname(os.path.abspath(__file__))
W, H = 640, 274
ctx = moderngl.create_standalone_context(require=330)


def make(path):
    src = open(os.path.join(here, path), encoding="utf-8").read()
    prog = ctx.program(vertex_shader=VERT, fragment_shader="#version 330 core\n#define NB %d\n" % K + src)
    return prog, ctx.vertex_array(prog, [])


fbo = ctx.framebuffer(ctx.texture((W, H), 4))
bands = np.full(K, 0.4, np.float32)
tex = ctx.texture((K, 1), 1, bands.tobytes(), dtype="f4"); tex.filter = (moderngl.LINEAR, moderngl.LINEAR); tex.use(0)


def frame(prog, vao, **u):
    d = dict(uRes=(W, H), uTime=3.0, uPhase=2.0, uRot=0.4, uBass=0.2, uMid=0.2, uTreb=0.1, uBeat=0.0, uLevel=0.3, uIdle=0.0,
             uA=(0.4, 0.6, 1.0), uB=(0.7, 0.3, 1.0), uC=(1.0, 0.5, 0.6))
    d.update(u)
    for k, v in d.items():
        if k in prog: prog[k].value = v
    if "uBandTex" in prog: prog["uBandTex"].value = 0
    fbo.use(); fbo.clear(0, 0, 0, 1); vao.render(moderngl.TRIANGLES, vertices=3)
    return np.frombuffer(fbo.read(components=3), np.uint8).astype(np.float32).reshape(H, W, 3)


for path in sys.argv[1:]:
    prog, vao = make(path)
    base = frame(prog, vao)
    kick = frame(prog, vao, uBass=0.9, uMid=0.6, uBeat=1.0, uLevel=0.8)       # the music jumps in one frame (everything else equal)
    spectrum = frame(prog, vao, uBass=0.2)                                      # the same, except the bass is smoothed in the real app
    t1 = frame(prog, vao, uPhase=2.0 + 0.4)                                     # 0.4 of phase = what the old app moved in ~0.15 s at a loud bass
    print("%-18s kick-jump change %.1f | phase step (0.4) change %.1f | mean brightness %.1f"
          % (path, np.abs(kick - base).mean(), np.abs(t1 - base).mean(), base.mean()))
