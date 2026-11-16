"""TECHNO の資産を Amiga 用に変換する。

- _CIRCLE.OBK  : OMF の DATA__circle  (24000 = 3 planes x 320x200/8)
- _CIRCLE2.OBK : OMF の DATA__circle2 ( 8000 = 1 plane)
- TROLL.UP     : readp 形式の 320x400x256 画像

出力:
  assets/techno/circle123.bin  24000  (plane 1..3)
  assets/techno/circle0.bin     8000  (plane 0)
  assets/techno/troll.bin    128000   (320x400, 8bit index)
  assets/techno/troll.pal       768   (8bit RGB)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from omf_stmik import OMF

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "reference", "SecondReality", "TECHNO")
OUT = os.path.join(ROOT, "assets", "techno")


def extract_obk(name, out_name):
    data = open(os.path.join(REF, name), "rb").read()
    omf = OMF(data)
    omf.parse()
    seg = omf.segments.get(1)
    if not seg:
        raise SystemExit("segment not found: " + name)
    open(os.path.join(OUT, out_name), "wb").write(seg)
    print("%s -> %s (%d bytes)" % (name, out_name, len(seg)))


def decode_troll():
    data = bytearray(open(os.path.join(REF, "TROLL.UP"), "rb").read())
    magic, wid, hig, cols, add = struct.unpack_from("<5h", data, 0)
    print("TROLL header: magic=%d wid=%d hig=%d cols=%d add=%d" %
          (magic, wid, hig, cols, add))
    pal = data[16:16 + cols * 3]
    pal8 = bytes(min(255, v * 255 // 63) for v in pal)

    src = add * 16
    img = bytearray(wid * hig)
    for row in range(hig):
        if row > 0:
            w0, w1 = struct.unpack_from("<hh", data, src)
            if w0 >= 0x4000:
                w0 -= 0x4000
                w1 += 0x400
                struct.pack_into("<hh", data, src, w0, w1)
        n = struct.unpack_from("<h", data, src)[0]
        p = src + 2
        end = p + n
        out = []
        while p < end:
            b = data[p]
            p += 1
            if b & 0x80:
                cnt = b & 0x7f
                v = data[p]
                p += 1
                out.extend([v] * cnt)
            else:
                out.append(b)
        if len(out) != wid:
            print("row %d: decoded %d (bytes %d)" % (row, len(out), n))
        img[row * wid:(row + 1) * wid] = bytes(out[:wid])
        src += n + 2
    open(os.path.join(OUT, "troll.bin"), "wb").write(img)
    open(os.path.join(OUT, "troll.pal"), "wb").write(pal8)
    print("troll.bin 128000, troll.pal 768")


def export_sin():
    """SIN1024.INC の dw 値を 1024 x s16 BE にする。

    do_interference の `_sin1024[bp]` は MASM のスケールなし添字なので、
    bp は **バイトオフセット**。`add bp,9*2` / `and bp,2047` は
    1,024 語 (2,048 バイト) の表をそのまま読み切る。
    （フレーム側は `shl bx,1` してから読むので、そちらは語添字。）
    """
    vals = []
    for line in open(os.path.join(REF, "SIN1024.INC")):
        line = line.strip()
        if not line.lower().startswith("dw"):
            continue
        for x in line[2:].split(","):
            x = x.strip()
            if x:
                vals.append(int(x))
    if len(vals) != 1024:
        raise SystemExit("SIN1024: %d entries" % len(vals))
    out = bytearray()
    for v in vals:
        out += struct.pack(">h", v)
    open(os.path.join(OUT, "sin1024.bin"), "wb").write(out)
    print("sin1024.bin 2048 (%d entries, min %d max %d)" %
          (len(vals), min(vals), max(vals)))


def main():
    os.makedirs(OUT, exist_ok=True)
    extract_obk("_CIRCLE.OBK", "circle123.bin")
    extract_obk("_CIRCLE2.OBK", "circle0.bin")
    export_sin()
    decode_troll()


if __name__ == "__main__":
    main()
