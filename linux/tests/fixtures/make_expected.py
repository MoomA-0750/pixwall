# 期待する絵の印（tests/fixtures/expected.txt）を Pillow で作る。GIF を足したら python3 -I tests/fixtures/make_expected.py
# 1行に「ファイル 幅 高さ 左上の色 フレームごとの FNV-1a（RGBA。透明なところは 0）」
import sys
from PIL import Image

FILES = ["../samples/dot-64x64-30f.gif", "tests/fixtures/disposal.gif", "tests/fixtures/interlaced.gif"]

def fnv(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

lines = []
for path in FILES:
    im = Image.open(path)
    w, h = im.size
    hashes = []
    top_left = 0
    for i in range(im.n_frames):
        im.seek(i)
        px = bytearray(im.convert("RGBA").tobytes())
        for p in range(0, len(px), 4):
            if px[p + 3] == 0:
                px[p:p + 4] = b"\0\0\0\0"
        if i == 0:
            top_left = (px[0] << 16) | (px[1] << 8) | px[2]
        hashes.append("%016x" % fnv(px))
    lines.append(" ".join([path, str(w), str(h), "%06x" % top_left] + hashes))
open("tests/fixtures/expected.txt", "w").write("\n".join(lines) + "\n")
