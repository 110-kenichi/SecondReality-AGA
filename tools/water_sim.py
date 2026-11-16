"""WATER (RAYSCRL) の putrouts1 を Python でシミュレーションして確認する。"""
import struct
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
WATER = ROOT / "assets" / "water"
SRC = ROOT / "reference" / "SecondReality" / "WATER"

bg = (WATER / "water_bg.bin").read_bytes()
pal = (WATER / "water_pal.bin").read_bytes()
font = (WATER / "water_font.bin").read_bytes()
lists = []
for i in range(1, 4):
    d = (SRC / f"WAT{i}.DAT").read_bytes()
    off = 0
    ent = []
    for e in range(34 * 158):
        n = struct.unpack_from("<H", d, off)[0]
        off += 2
        pos = struct.unpack_from("<%dH" % n, d, off) if n else ()
        off += 2 * n
        ent.append(pos)
    lists.append(ent)

FB_W, FB_H = 158, 34
FB_SIZE = FB_W * FB_H


def sim(frames=600):
    fbuf = bytearray(FB_SIZE)
    scp = 0
    sss = 0
    out = []
    for f in range(frames):
        scr = bytearray(bg)
        lst = lists[sss]
        for e in range(FB_SIZE):
            c = fbuf[e]
            if c:
                for pos in lst[e]:
                    scr[pos] = c
        out.append((f, bytes(scr), scp, sss))
        if sss == 2:
            for row in range(FB_H):
                r = row * FB_W
                fbuf[r:r + FB_W - 1] = fbuf[r + 1:r + FB_W]
                fbuf[r + FB_W - 1] = font[row * 400 + scp]
            if scp < 390:
                scp += 1
            sss = 0
        else:
            sss += 1
    return out


def save(scr, path):
    im = Image.new("RGB", (320, 200))
    px = im.load()
    for y in range(200):
        for x in range(320):
            c = scr[y * 320 + x]
            px[x, y] = (pal[c * 3], pal[c * 3 + 1], pal[c * 3 + 2])
    im.resize((640, 400), Image.NEAREST).save(path)


out = sim(frames=600)
diffs = []
for i in range(1, len(out)):
    a = out[i - 1][1]
    b = out[i][1]
    diffs.append(sum(1 for x, y in zip(a, b) if x != y))
print("frames", len(out), "max diff", max(diffs), "avg", sum(diffs) / len(diffs))
for idx in (0, len(out) // 4, len(out) // 2, len(out) * 3 // 4):
    f, scr, scp, sss = out[idx]
    save(scr, ROOT / "run" / f"sim-{f}.png")
    print("  saved", f"sim-{f}.png", "scp", scp, "sss", sss)
