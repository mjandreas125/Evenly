# Offscreen check of NebulaRenderer::renderClip: the same shaders and the same ribbon geometry, on lines written by ClipTest.
# python tools/preview_clip_gl.py lines.txt out_dir [width height]
import os, sys
import numpy as np
import moderngl
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
W = int(sys.argv[3]) if len(sys.argv) > 3 else 1280
H = int(sys.argv[4]) if len(sys.argv) > 4 else 720
os.makedirs(out, exist_ok=True)
frames, cur = [], None
for row in open(src):
    p = row.split()
    if p[0] == 'frame': cur = (float(p[1]), []); frames.append(cur)
    else: cur[1].append((np.array([float(x) for x in p[1:4]]), float(p[4]), np.array([float(x) for x in p[5:]], dtype=np.float32).reshape(-1, 2)))

ctx = moderngl.create_standalone_context(require=330)
prog = ctx.program(
    vertex_shader="#version 330 core\nin vec2 aPos; in float aAcross; out float vAcross;\nvoid main(){ vAcross = aAcross; gl_Position = vec4(aPos, 0.0, 1.0); }\n",
    fragment_shader="#version 330 core\nuniform vec3 uCol; uniform float uSharp; in float vAcross; out vec4 o;\nvoid main(){ float e = exp(-vAcross * vAcross * uSharp); o = vec4(uCol * e, 1.0); }\n")
fbo = ctx.simple_framebuffer((W, H))
k = H / 540.0
core = max(1.1, 1.6 + 1.0 * k); glow = core * 6.0
picked = frames if len(sys.argv) > 5 and sys.argv[5] == 'all' else [f for f in frames if any(abs(f[0] - x) < 0.3 for x in (9.96, 38.99, 43.27, 49.98, 50.46, 58.52))]
for t, lines in picked:
    fbo.use(); ctx.clear(0.02, 0.02, 0.025, 1.0)
    ctx.enable(moderngl.BLEND); ctx.blend_func = (moderngl.ONE, moderngl.ONE)
    for col, a, pts in reversed(lines):
        if a < 0.01: continue
        P = pts * (H * 0.5)
        for half, s, sharp, reach in ((glow, 0.17 * a, 1.6, 2), (core, 0.85 * a, 1.7, 1)):      # (ClipTest writes every 2nd point: reach 4 / 2 there)
            idx = np.arange(len(P)); T = P[np.minimum(idx + reach, len(P) - 1)] - P[np.maximum(idx - reach, 0)]
            T /= (np.linalg.norm(T, axis=1, keepdims=True) + 1e-6)
            Nn = np.stack([-T[:, 1], T[:, 0]], 1)
            # no wider than the bend is round (as renderClip)
            A = P[1:-1] - P[:-2]; B = P[2:] - P[1:-1]
            cross = np.abs(A[:, 0] * B[:, 1] - A[:, 1] * B[:, 0])
            R = np.linalg.norm(A, axis=1) * np.linalg.norm(B, axis=1) * np.linalg.norm(A + B, axis=1) / (2 * cross + 1e-9)
            hw = np.full(len(P), half); hw[1:-1] = np.minimum(half, np.maximum(core, 0.9 * R))
            Nn = Nn * hw[:, None] / half
            v = np.zeros((len(P) * 2, 3), np.float32)
            v[0::2, :2] = (P + Nn * half) / [W * 0.5, H * 0.5]; v[0::2, 2] = 1
            v[1::2, :2] = (P - Nn * half) / [W * 0.5, H * 0.5]; v[1::2, 2] = -1
            vbo = ctx.buffer(v.tobytes())
            vao = ctx.vertex_array(prog, [(vbo, '2f 1f', 'aPos', 'aAcross')])
            prog['uCol'].value = tuple(float(c) * s for c in col); prog['uSharp'].value = sharp
            vao.render(moderngl.TRIANGLE_STRIP)
            vao.release(); vbo.release()
    img = Image.frombytes('RGB', (W, H), fbo.read(components=3)).transpose(Image.FLIP_TOP_BOTTOM)
    img.save(os.path.join(out, f'gl_{t:06.2f}.png'))
print(len(picked), 'frames')
