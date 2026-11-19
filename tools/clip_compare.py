# Offline check of the "Clip" picture against the real video (docs/diwk): makes ClipLine inputs from the recorded sound and the lyrics,
# runs ClipTest, and draws each of its frames under the video frame of the same moment.
# python tools/clip_compare.py <docs/diwk> <ClipTest.exe> <out dir> [lyrics shift] [story_from_seconds]
import glob, json, math, os, re, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

diwk, exe, out = sys.argv[1], sys.argv[2], sys.argv[3]
shift = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
story_from = float(sys.argv[5]) if len(sys.argv) > 5 else 30.0
chorus_from = float(sys.argv[6]) if len(sys.argv) > 6 else 1e9      # pretend a chorus from here on (the recording has none)
os.makedirs(out, exist_ok=True)
rows = sorted(json.load(open(os.path.join(diwk, 'sound.json')))['rows'], key=lambda r: r[0])
t = np.array([r[0] for r in rows]); b = np.array([r[3] for r in rows]); fl = np.array([r[4] for r in rows]); mid = np.array([r[1] for r in rows])
fps = 60
grid = np.arange(0, t.max(), 1 / fps)
def g(x): return np.interp(grid, t, x)
flux = g(fl); voice = g(b[:, 3]); low = g(b[:, 0] + b[:, 1]); lowmid = g(b[:, 2]); high = g(b[:, 4]); lev = g(mid)
def n(x): return np.clip(x / (np.percentile(x, 98) or 1), 0, 1)
flux_n, voice_n, high_n, lev_n, lowmid_n = n(flux), n(voice), n(high), n(lev), n(lowmid)

# onsets: peaks of the flux above its local average
onset = np.zeros(len(grid), bool)
avg = np.convolve(flux_n, np.ones(30) / 30, 'same')
last = -1
for i in range(1, len(grid) - 1):
    if flux_n[i] > avg[i] * 1.8 + 0.08 and flux_n[i] >= flux_n[i - 1] and flux_n[i] >= flux_n[i + 1] and i - last > 6:
        onset[i] = True; last = i

lines = []
for line in open(os.path.join(diwk, 'lyrics_album_lrclib.lrc'), encoding='utf-8-sig'):
    m = re.match(r'\[(\d+):(\d+\.\d+)\]\s*(.*)', line.strip())
    if m: lines.append((int(m.group(1)) * 60 + float(m.group(2)) - shift, m.group(3)))
# the drawing each line calls for: the same rule as ClipLine::shapeFor, done by the test exe would need a second entry; here a copy
src = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'Source', 'app', 'ClipShapes.h'), encoding='utf-8').read()
shapes = re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"([^"]+)"\s*\}', src)
def shape_for(text):
    words = [w[:-1] + 'g' if w.endswith("in'") and len(w) > 3 else w for w in re.findall(r"[\w']+", text.lower())]
    score = [0.0] * len(shapes); used = set()
    for w in words:
        best = None
        for k, (_, keys, _) in enumerate(shapes):
            for idx, key in enumerate(keys.split()):
                wt = 1.0
                if key.startswith('~'): wt, key = 0.35, key[1:]
                extra = 10 if ord(key[0]) > 127 else (1 if len(key) <= 3 else 3)
                if w.startswith(key) and len(w) - len(key) <= extra and (best is None or len(key) > best[2]): best = (k, idx, len(key), wt)
        if best and (best[0], best[1]) not in used: used.add((best[0], best[1])); score[best[0]] += best[3]
    k = int(np.argmax(score)); return k if score[k] > 0.99 else -1
line_shape = [shape_for(x) for _, x in lines]
print('drawings per line:', [(round(a, 1), shapes[k][0]) for (a, _), k in zip(lines, line_shape) if k >= 0])

frames = sorted((float(re.search(r'f_([\d.]+)\.jpg', p).group(1)), p) for p in glob.glob(os.path.join(diwk, 'frames', 'f_*.jpg')))
open(os.path.join(out, 'times.txt'), 'w').write('\n'.join(f'{ft:.2f}' for ft, _ in frames))
aspect = 16 / 9
with open(os.path.join(out, 'inputs.txt'), 'w') as f:
    li_prev = -1
    for i, tt in enumerate(grid):
        li = max([k for k, (a, x) in enumerate(lines) if a <= tt] or [-1])
        txt = lines[li][1] if li >= 0 else ''
        nxt = lines[li + 1][0] if li + 1 < len(lines) else tt + 4
        sung_end = (lines[li][0] + min(nxt - lines[li][0] - 0.1, 0.33 * len(txt.split()) + 0.6)) if li >= 0 and txt else -1
        singing = li >= 0 and bool(txt) and tt < sung_end
        started = li != li_prev and li >= 0 and bool(txt); li_prev = li
        next_in = min([a - tt for a, x in lines if a > tt and x] or [99])
        voiced = 1.0 if singing else (0.8 if tt > 6.0 and lowmid_n[i] > 0.25 else 0.0)   # the guitar comes in at 6 s
        f.write(f"{tt:.4f} {int(onset[i])} {flux_n[i]:.3f} {high_n[i]:.3f} {voiced:.2f} {voice_n[i]:.3f} 0 0.5 {lev_n[i]:.3f} 1 {int(started)} {int(singing)} "
                f"{int('crawlin' in txt.lower() or 'ever thought' in txt.lower() or tt > chorus_from)} {int(txt.startswith('('))} {max(0.0, nxt - tt):.2f} {line_shape[li] if li >= 0 else -1} {int(tt > story_from)} {aspect:.4f} {next_in:.2f}\n")
subprocess.run([exe, os.path.join(out, 'inputs.txt'), os.path.join(out, 'times.txt'), os.path.join(out, 'lines.txt')], check=True)

# draw: video frame on top, ours below, six moments per sheet
ours = {}
cur = None
for row in open(os.path.join(out, 'lines.txt')):
    p = row.split()
    if p[0] == 'frame': cur = float(p[1]); ours[cur] = []
    else: ours[cur].append(([float(x) for x in p[1:4]], float(p[4]), np.array([float(x) for x in p[5:]]).reshape(-1, 2)))
W, H = 480, 270
items = [(ft, pth) for ft, pth in frames if any(abs(ft - k) < 0.02 for k in ours)]
for s in range(0, len(items), 12):
    sheet = Image.new('RGB', (W * 6, (H * 2 + 24) * 2), (30, 30, 30)); d = ImageDraw.Draw(sheet)
    for j, (ft, pth) in enumerate(items[s:s + 12]):
        x0, y0 = (j % 6) * W, (j // 6) * (H * 2 + 24)
        sheet.paste(Image.open(pth).convert('RGB').resize((W, H)), (x0, y0 + 20))
        d.text((x0 + 4, y0 + 3), f'{ft:.2f}  video / ours', fill=(255, 255, 0))
        key = min(ours, key=lambda k: abs(k - ft))
        img = Image.new('RGB', (W, H), (8, 8, 8)); dd = ImageDraw.Draw(img)
        for col, a, pts in ours[key]:
            c = tuple(int(255 * v * min(1, a)) for v in col)
            P = [((x / aspect + 1) * W / 2, (1 - y) * H / 2) for x, y in pts]
            dd.line(P, fill=c, width=3)
        sheet.paste(img, (x0, y0 + 20 + H))
    sheet.save(os.path.join(out, f'compare_{s // 12:02d}.jpg'), quality=85)
print('sheets', (len(items) + 11) // 12)
