"""Draws the app icon: a dark rounded square with level bars, uneven and dim on the left, equal and
bright (warm amber) on the right, i.e. jumpy sound made even. Writes Resources/icon-{256,32}.png.
Usage: python make_icon.py <Resources dir>"""
import os
import sys

from PIL import Image, ImageDraw


def icon(size):
    s = 4  # supersample
    W = size * s
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(W * 0.22)
    d.rounded_rectangle([0, 0, W - 1, W - 1], radius=r, fill=(22, 24, 30, 255))
    # five vertical bars centred on a line: two uneven dim ones, then three equal bright ones
    heights = [0.30, 0.62, 0.44, 0.44, 0.44]
    colors = [(88, 92, 104), (88, 92, 104), (255, 176, 84), (255, 138, 72), (255, 104, 70)]
    n, bar_w, gap = len(heights), W * 0.105, W * 0.06
    total = n * bar_w + (n - 1) * gap
    x = (W - total) / 2
    cy = W / 2
    for h, c in zip(heights, colors):
        half = W * h / 2
        d.rounded_rectangle([x, cy - half, x + bar_w, cy + half], radius=int(bar_w / 2), fill=c + (255,))
        x += bar_w + gap
    return img.resize((size, size), Image.LANCZOS)


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for n in (256, 32):
    icon(n).save(os.path.join(out, f"icon-{n}.png"))
print("ok")
