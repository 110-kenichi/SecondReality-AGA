"""FOREST (MNTSCRL) の putrouts を Python でシミュレーションする。"""
import struct
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "reference" / "SecondReality" / "FOREST"
ASSETS = ROOT / "assets" / "forest"

bg = (ASSETS / "forest_bg.bin").read_bytes()
pal = (ASSETS / "forest_pal.bin").read_bytes()
mask = (ASSETS / "forest_mask.bin").read_bytes()

lists = []
for i in range(1, 4):
    d = (SRC / f"POS{i}.DAT").read_bytes()
    off = 0
    ent = []
    for e in range(31 * 237):
        n = struct.unpack_from("<H", d, off)[0]
        off += 2
        pos = struct.unpack_from("<%dH" % n, d, off) if n else ()
        off += 2 * n
        ent.append(pos)
    lists.append(ent)

SCROLL_MAX = 2500


def sim(frames=120):
    scr = bytearray(bg)
    scp = 0
    sss = 0
    out = []
    for f in range(frames):
        lst = lists[sss]
        for e in range(31 * 237):
            add = mask[scp + e]
            for pos in lst[e]:
                scr[pos] = (bg[pos] + add) & 0xFF
        out.append((f, bytes(scr), scp, sss))
        if sss == 2:
            if scp < SCROLL_MAX:
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


out = sim(120)
diffs = []
for i in range(1, len(out)):
    a = out[i - 1][1]
    b = out[i][1]
    diffs.append(sum(1 for x, y in zip(a, b) if x != y))
print("frames", len(out), "max diff", max(diffs), "avg", sum(diffs) / len(diffs))
for idx in (0, 30, 60, 90):
    f, scr, scp, sss = out[idx]
    save(scr, ROOT / "run" / f"forest-sim-{f}.png")
    print("saved", f"forest-sim-{f}.png", "scp", scp, "sss", sss)
