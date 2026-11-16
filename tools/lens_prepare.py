"""Convert Second Reality LENS (LNS&ZOOM) data for the Amiga port.

Inputs (reference/SecondReality/LENS):
  LENS.EXB : 16-byte header + 768 palette + 64000 picture (320x200, 64 colors)
  LENS.EX0 : int16 lenswid, int16 lenshig, 3x RGB offset (6-bit), text...
  LENS.EX1..EX4 : per-row display lists (16-bit LE words)
  LENS.EXP : u16 0, u16 path1_words, path1 (int16 pairs), path2 (int16 quads)

Outputs (assets/lens):
  lens_back.bin  : 64000 bytes  (320x200 8-bit, 0..63)
  lens_pal.bin   : 768 bytes    (8-bit RGB, 6bit<<2)
  lens_ex0.bin   : big-endian u16[2] dims + 9 bytes RGB offsets
  lens_ex1.bin .. lens_ex4.bin : big-endian u16 lists
  lens_path.bin  : big-endian u16: [n1][path1...][path2...]
"""
import struct
from pathlib import Path

REF = Path(r"reference/SecondReality/LENS")
OUT = Path(r"assets/lens")
OUT.mkdir(parents=True, exist_ok=True)

exb = (REF / "LENS.EXB").read_bytes()
assert len(exb) == 64784, len(exb)
pal6 = exb[16:16 + 768]
pal8 = bytes(min(255, v << 2) for v in pal6)

# 背景は LENSPICX.LBM (LENSPIC + 額の五芒星) を使う。
# LENS.EXB の画像は五芒星なしの開発版で、製品版は X 付き。
def _decode_lbm(path):
    d = path.read_bytes()
    assert d[:4] == b"FORM"
    pos = 12
    chunks = {}
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        sz = struct.unpack_from(">I", d, pos + 4)[0]
        chunks[cid] = d[pos + 8:pos + 8 + sz]
        pos += 8 + sz + (sz & 1)
    bm = chunks[b"BMHD"]
    w, h, _x, _y, planes, _mask, comp = struct.unpack_from(">HHhhBBB", bm, 0)
    body = chunks[b"BODY"]
    if comp == 1:
        out = bytearray()
        i = 0
        while i < len(body):
            b = body[i]
            i += 1
            if b <= 127:
                out += body[i:i + b + 1]
                i += b + 1
            elif b >= 129:
                out += bytes([body[i]]) * (257 - b)
                i += 1
        body = bytes(out)
    # PBM (8bit chunky) / ILBM (planar)
    if d[8:12] == b"PBM ":
        return bytes(body[:w * h])
    plane = w // 8 * h
    chunky = bytearray(w * h)
    for y in range(h):
        row = y * (w // 8)
        for x in range(w):
            v = 0
            for pl in range(planes):
                if body[pl * plane + row + (x >> 3)] & (0x80 >> (x & 7)):
                    v |= 1 << pl
            chunky[y * w + x] = v
    return bytes(chunky)

# 製品版 SECOND.EXE (LNS&ZOOM.EXE) から復元した最終画像を使う。
# 開発版 LENS.EXB / LENSPIC.LBM には額の五芒星がないため、
# 製品版の PIC データ (64000 bytes, 0..63) を LENSPIC.FINAL として保存した。
# 復元手順は tools/extract_second.py を参照。
final = REF / "LENSPIC.FINAL"
if final.exists():
    pic = final.read_bytes()
elif (OUT / "lens_back.bin").exists():
    # 製品版画像は assets/lens/lens_back.bin としてリポジトリに入っている。
    # reference/ が無い環境ではそれをそのまま使う。
    pic = (OUT / "lens_back.bin").read_bytes()
else:
    pic = _decode_lbm(REF / "LENSPIC.LBM")
assert len(pic) == 64000, len(pic)



ex0 = (REF / "LENS.EX0").read_bytes()
lenswid, lenshig = struct.unpack_from("<hh", ex0, 0)
assert lenswid > 0 and lenshig > 0
dims = struct.pack(">hh", lenswid, lenshig) + ex0[4:13]

def conv_16le(data: bytes) -> bytes:
    n = len(data) // 2
    v = struct.unpack("<%dH" % n, data[:n * 2])
    return struct.pack(">%dH" % n, *v)

exp = (REF / "LENS.EXP").read_bytes()
zero, n1 = struct.unpack_from("<HH", exp, 0)
assert zero == 0
path1 = exp[4:4 + 2 * n1]
path2 = exp[4 + 2 * n1:]
assert len(path1) % 4 == 0 and len(path2) % 8 == 0

(OUT / "lens_back.bin").write_bytes(pic)
(OUT / "lens_pal.bin").write_bytes(pal8)
(OUT / "lens_ex0.bin").write_bytes(dims)
for i in range(1, 5):
    raw = (REF / ("LENS.EX%d" % i)).read_bytes()
    conv = conv_16le(raw)
    assert len(conv) == len(raw)
    (OUT / ("lens_ex%d.bin" % i)).write_bytes(conv)

path = struct.pack(">H", n1) + conv_16le(path1) + conv_16le(path2)
(OUT / "lens_path.bin").write_bytes(path)

print("lenswid=%d lenshig=%d" % (lenswid, lenshig))
print("path1 words=%d (%d frames), path2 words=%d (%d frames)" %
      (n1, n1 // 2, len(path2) // 2, len(path2) // 8))
for i in range(1, 5):
    raw = (REF / ("LENS.EX%d" % i)).read_bytes()
    print("EX%d: %d bytes" % (i, len(raw)))
print("wrote", OUT)
