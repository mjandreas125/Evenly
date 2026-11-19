# Audit: each moment of the song as the video shows it (left) and as the Clip picture draws it (right), same renderer as the app.
# python tools/audit_clip.py frames.txt out.png recorded_frames_dir early_frames_dir
import os, sys, glob
import numpy as np, moderngl
from PIL import Image, ImageDraw
src, outp, recdir, earlydir = sys.argv[1:5]
W, H = 640, 360
frames, cur = [], None
for row in open(src):
    p = row.split()
    if p[0] == 'frame': cur = (float(p[1]), [float(x) for x in p[2:5]], int(p[5]), []); frames.append(cur)
    else: cur[3].append((np.array([float(x) for x in p[1:4]]), float(p[4]), np.array([float(x) for x in p[5:]], dtype=np.float32).reshape(-1, 2)))
ctx = moderngl.create_standalone_context(require=330)
prog = ctx.program(
    vertex_shader="#version 330 core\nin vec2 aPos; in float aAcross; out float vAcross;\nvoid main(){ vAcross = aAcross; gl_Position = vec4(aPos, 0.0, 1.0); }\n",
    fragment_shader="#version 330 core\nuniform vec3 uCol; uniform float uSharp; in float vAcross; out vec4 o;\nvoid main(){ float e = exp(-vAcross * vAcross * uSharp); o = vec4(uCol * e, 1.0); }\n")
fbo = ctx.simple_framebuffer((W, H))
k = H / 540.0; core = max(1.1, 1.6 + 1.0 * k); glow = core * 6.0
def render(bg, ink, lines):
    fbo.use(); ctx.clear(0.02 + bg[0], 0.02 + bg[1], 0.025 + bg[2], 1.0)
    ctx.enable(moderngl.BLEND)
    ctx.blend_func = (moderngl.ZERO, moderngl.ONE_MINUS_SRC_COLOR) if ink else (moderngl.ONE, moderngl.ONE)
    for col, a, pts in reversed(lines):
        P = pts * (H * 0.5)
        for half, s, sharp, reach in ((glow, 0.17 * a, 1.6, 2), (core, 0.85 * a, 1.7, 1)):
            idx = np.arange(len(P)); T = P[np.minimum(idx + reach, len(P) - 1)] - P[np.maximum(idx - reach, 0)]
            T /= (np.linalg.norm(T, axis=1, keepdims=True) + 1e-6)
            Nn = np.stack([-T[:, 1], T[:, 0]], 1)
            v = np.zeros((len(P) * 2, 3), np.float32)
            v[0::2, :2] = (P + Nn * half) / [W * 0.5, H * 0.5]; v[0::2, 2] = 1
            v[1::2, :2] = (P - Nn * half) / [W * 0.5, H * 0.5]; v[1::2, 2] = -1
            vbo = ctx.buffer(v.tobytes()); vao = ctx.vertex_array(prog, [(vbo, '2f 1f', 'aPos', 'aAcross')])
            c = (s, s, s) if ink else tuple(float(x) * s for x in col)
            prog['uCol'].value = c; prog['uSharp'].value = sharp
            vao.render(moderngl.TRIANGLE_STRIP); vao.release(); vbo.release()
    return Image.frombytes('RGB', (W, H), fbo.read(components=3)).transpose(Image.FLIP_TOP_BOTTOM)
def clipframe(t):
    if t >= 59.0:
        i = int(round((t - 59.0) * 2)) + 1
        f = os.path.join(recdir, 'r_%04d.png' % i)
        if os.path.exists(f): return Image.open(f).convert('RGB').resize((W, H))
    fs = sorted(glob.glob(os.path.join(earlydir, 'f_*.jpg')))
    if fs:
        best = min(fs, key=lambda f: abs(float(os.path.basename(f)[2:-4]) - t))
        return Image.open(best).convert('RGB').resize((W, H))
    return Image.new('RGB', (W, H))
rows = []
for t, bg, ink, lines in frames:
    pair = Image.new('RGB', (W * 2 + 6, H + 18), (40, 40, 40))
    pair.paste(clipframe(t), (0, 18)); pair.paste(render(bg, ink, lines), (W + 6, 18))
    ImageDraw.Draw(pair).text((4, 2), f'{t:6.1f} s   video | Evenly', fill=(230, 210, 80))
    rows.append(pair)
cols = 2
sheet = Image.new('RGB', ((W * 2 + 6) * cols + 10, (H + 18) * ((len(rows) + cols - 1) // cols)), (20, 20, 20))
for i, r in enumerate(rows): sheet.paste(r, ((i % cols) * ((W * 2 + 6) + 10), (i // cols) * (H + 18)))
sheet.save(outp); print(len(rows), 'moments')
