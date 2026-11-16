"""Prepare PLZPART's plasmacube assets (PLZPART/VECT.C + PLZFILL.C).

Outputs (assets/plasma):
  cube_tex.bin    : 3 x 128x256 plasma textures (kuva1/2/3)
  cube_pal.bin    : 768 bytes (3 x 64-colour blocks, 6bit->8bit)
  cube_spline.bin : N x 8 s16 BE control points
                    (tx,ty,dis,kx,ky,kz,ls_kx,ls_ky)
"""
import math
import re
import struct
from pathlib import Path

REF = Path(r"reference/SecondReality/PLZPART")
OUT = Path(r"assets/plasma")
OUT.mkdir(parents=True, exist_ok=True)


def build_textures():
    sin = [0] * 1524
    for a in range(1524):
        sin[a] = int(math.sin(a / 1024.0 * math.pi * 4.0) * 127)
    tex = bytearray()
    for layer in range(3):
        base = 32 + layer * 64
        for y in range(128):
            for x in range(256):
                v = sin[(y * 4 + sin[x * 2]) & 511] // 4 + base
                tex.append(max(0, min(255, v)))
    return bytes(tex)


def build_palette():
    pal = bytearray(768)

    def put(idx, r, g, b):
        pal[idx * 3 + 0] = (r * 255) // 63
        pal[idx * 3 + 1] = (g * 255) // 63
        pal[idx * 3 + 2] = (b * 255) // 63

    # block 0: blue/white
    for a in range(1, 32):
        put(a, 0, 0, a * 2)
    for a in range(32):
        put(32 + a, a * 2, a * 2, 63)
    # block 1: red/yellow
    for a in range(32):
        put(64 + a, a * 2, 0, 0)
    for a in range(32):
        put(96 + a, 63, a * 2, 0)
    # block 2: orange/violet
    for a in range(32):
        put(128 + a, a, 0, a * 2 // 3)
    for a in range(32):
        put(160 + a, 31 - a, a * 2, 21)
    return bytes(pal)


def build_spline():
    kkk = 100
    points = []
    last = None
    repeat = 0
    for raw in (REF / "RATA.INC").read_text(errors="replace").splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        up = line.upper()
        if up.startswith("KKK"):
            m = re.search(r"=\s*(\d+)", line)
            if m:
                kkk = int(m.group(1))
            continue
        if up.startswith("REPT"):
            repeat = int(line.split()[1])
            continue
        if up.startswith("ENDM"):
            if repeat and last:
                points.extend([last] * repeat)
            repeat = 0
            continue
        if up.startswith("DW"):
            vals = [int(eval(v, {"kkk": kkk})) for v in line[2:].split(",")]
            if len(vals) != 8:
                continue
            last = vals
            points.append(vals)
    blob = b"".join(struct.pack(">8h", *p) for p in points)
    return blob, len(points)


def build_splinecoef():
    """SPLINE.INC: 1024 words = 4 blocks of 256 (cubic basis for getspl)."""
    txt = (REF / "SPLINE.INC").read_text(errors="replace")
    vals = [int(v) for v in re.findall(r"-?\d+", txt.replace(";", "\n"))]
    # the leading number is the count comment; keep the 1024 coefficients
    vals = [v for v in vals if -32768 <= v <= 32767]
    if len(vals) > 1024:
        vals = vals[-1024:]
    assert len(vals) == 1024, len(vals)
    # sanity: each t should sum to ~32768
    for t in (0, 128, 255):
        s = vals[t] + vals[t + 256] + vals[t + 512] + vals[t + 768]
        assert abs(s - 32768) < 16, (t, s)
    return b"".join(struct.pack(">h", v) for v in vals)


tex = build_textures()
pal = build_palette()
spl, n = build_spline()
splcoef = build_splinecoef()
sin14 = b"".join(struct.pack(">h", int(round(math.sin(i * 2 * math.pi / 1024) * 16384)))
                for i in range(1024))
(OUT / "cube_tex.bin").write_bytes(tex)
(OUT / "cube_pal.bin").write_bytes(pal)
(OUT / "cube_spline.bin").write_bytes(spl)
(OUT / "cube_splcoef.bin").write_bytes(splcoef)
(OUT / "cube_sin.bin").write_bytes(sin14)
print("cube_tex.bin %d bytes (3 x 128x256)" % len(tex))
print("cube_pal.bin %d bytes" % len(pal))
print("cube_spline.bin %d bytes (%d points)" % (len(spl), n))
print("cube_splcoef.bin %d bytes" % len(splcoef))
print("cube_sin.bin %d bytes" % len(sin14))
