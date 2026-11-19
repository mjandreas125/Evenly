# Draws every one-line drawing of Source/app/ClipShapes.h the way the "Clip" picture uses it: the line comes in from the left edge,
# draws the shape, leaves to the right edge. Generated shapes (@sun, @flower, @spiral) follow ClipLine::generated.
# python tools/preview_clip_shapes.py out.png
import math, re, sys
from PIL import Image, ImageDraw

src = open(__file__.replace('tools\\preview_clip_shapes.py', 'Source\\app\\ClipShapes.h').replace('tools/preview_clip_shapes.py', 'Source/app/ClipShapes.h'), encoding='utf-8').read()
shapes = re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"([^"]+)"\s*\}', src)

def generated(name):
    pts = []
    if name == '@sun':
        for i in range(0, 97):                       # a circle, then rays as a zigzag around it
            a = math.pi + i / 96 * 2 * math.pi
            pts.append((50 + 22 * math.cos(a), 50 + 22 * math.sin(a)))
        for i in range(0, 25):
            a = math.pi + i / 24 * 2 * math.pi
            r = 30 if i % 2 == 0 else 45
            pts.append((50 + r * math.cos(a), 50 + r * math.sin(a)))
    elif name == '@flower':
        pts.append((50, 98)); pts.append((50, 58))
        for i in range(0, 241):                      # five petals: a rose curve r = cos(5t/2)... kept simple: r = |sin(2.5 t)|
            t = i / 240 * 2 * math.pi
            r = 30 * abs(math.sin(2.5 * t)) + 4
            pts.append((50 + r * math.cos(t - math.pi / 2), 38 + r * math.sin(t - math.pi / 2)))
        pts.append((50, 58)); pts.append((50, 70)); pts.append((70, 60))
    elif name == '@spiral':
        for i in range(0, 241):
            t = i / 240 * 6 * math.pi
            r = 2 + 42 * i / 240
            pts.append((50 + r * math.cos(t + math.pi), 50 + r * math.sin(t + math.pi)))
    return pts

def parse(path):
    if path.startswith('@'): return generated(path)
    tok = path.replace(',', ' ').split()
    pts, i, cur, cmd = [], 0, (0, 0), 'M'
    num = lambda: float(tok[i])
    while i < len(tok):
        if tok[i] in ('M', 'L', 'C', 'Q'): cmd = tok[i]; i += 1; continue
        if cmd in ('M', 'L'):
            cur = (float(tok[i]), float(tok[i + 1])); i += 2; pts.append(cur)
        elif cmd == 'C':
            p1 = (float(tok[i]), float(tok[i + 1])); p2 = (float(tok[i + 2]), float(tok[i + 3])); p3 = (float(tok[i + 4]), float(tok[i + 5])); i += 6
            for k in range(1, 17):
                t = k / 16; u = 1 - t
                pts.append((u**3 * cur[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0], u**3 * cur[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
            cur = p3
        elif cmd == 'Q':
            p1 = (float(tok[i]), float(tok[i + 1])); p2 = (float(tok[i + 2]), float(tok[i + 3])); i += 4
            for k in range(1, 13):
                t = k / 12; u = 1 - t
                pts.append((u * u * cur[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * cur[1] + 2 * u * t * p1[1] + t * t * p2[1]))
            cur = p2
    return pts

cols = 5
cw, ch = 360, 200
rows = (len(shapes) + cols - 1) // cols
img = Image.new('RGB', (cols * cw, rows * ch), (8, 8, 8))
d = ImageDraw.Draw(img)
for k, (name, words, path) in enumerate(shapes):
    ox, oy = (k % cols) * cw, (k // cols) * ch
    pts = parse(path)
    s = 1.5                                            # the drawing is 150 px of a 200 px cell, centred
    P = [(ox + cw / 2 + (x - 50) * s, oy + ch / 2 + (y - 50) * s) for x, y in pts]
    line = [(ox + 5, oy + ch / 2)] + P + [(ox + cw - 5, oy + ch / 2)]
    d.line(line, fill=(240, 240, 240), width=3, joint='curve')
    d.text((ox + 6, oy + 4), name, fill=(255, 210, 60))
img.save(sys.argv[1] if len(sys.argv) > 1 else 'clip_shapes.png')
print(len(shapes), 'shapes')
