"""WATER (RAYSCRL) 用アセット変換

入力 (reference/SecondReality/WATER/):
  BKG.CLX      : +10 に 768B パレット (VGA 6bit)、+778 に 320x200 背景
                 (FINAL.LBM と同一。DEMO.PAS の `tausta` = BKG.OBJ)
  MIEKKA.SCI   : +10 に 768B パレット (VGA 6bit)、+778 に 400x35 のフォント/マスク
  WAT1-3.DAT   : 34 レイヤ x 158 色のオフセットリスト (u16 LE)

出力 (assets/water/):
  water_bg.bin   : 320*200 = 64000 バイト (8bit インデックス)
  water_pal.bin  : 256*3 = 768 バイト (8bit RGB)
  water_font.bin : 400*35 = 14000 バイト
  water1.bin .. water3.bin : リストをビッグエンディアン化したもの
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "reference" / "SecondReality" / "WATER"
OUT = ROOT / "assets" / "water"


def parse_iff(d):
    assert d[0:4] == b"FORM"
    chunks = {}
    pos = 12
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        size = struct.unpack_from(">I", d, pos + 4)[0]
        chunks[cid] = d[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    return chunks


def decode_byterun1(src, expected):
    out = bytearray()
    i = 0
    n = len(src)
    while i < n and len(out) < expected:
        b = src[i]
        i += 1
        if b <= 127:
            cnt = b + 1
            out += src[i:i + cnt]
            i += cnt
        elif b >= 129:
            cnt = 257 - b
            if i < n:
                out += bytes([src[i]]) * cnt
                i += 1
    return bytes(out[:expected])


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    # --- 背景 (BKG.CLX = 10B ヘッダ + 768B パレット + 64000B ピクセル) ---
    # オリジナル DEMO.PAS の `tausta` は BKG.OBJ (= BKG.CLX) で、
    # TAUSTA.LBM ではない。BKG.CLX のピクセル列は FINAL.LBM と完全一致する。
    clx = (SRC / "BKG.CLX").read_bytes()
    assert len(clx) >= 778 + 64000, len(clx)
    raw = clx[778:778 + 320 * 200]
    assert len(raw) == 320 * 200
    (OUT / "water_bg.bin").write_bytes(raw)

    # --- パレット + フォント (MIEKKA.SCI) ---
    sci = (SRC / "MIEKKA.SCI").read_bytes()
    pal6 = sci[10:10 + 768]
    pal8 = bytes(min(255, v << 2) for v in pal6)
    (OUT / "water_pal.bin").write_bytes(pal8)
    font = sci[778:778 + 400 * 35]
    assert len(font) == 400 * 35
    (OUT / "water_font.bin").write_bytes(font)

    # --- WAT リスト (u16 LE -> BE) ---
    for i in range(1, 4):
        src = (SRC / f"WAT{i}.DAT").read_bytes()
        off = 0
        out = bytearray()
        for e in range(34 * 158):
            n = struct.unpack_from("<H", src, off)[0]
            off += 2
            out += struct.pack(">H", n)
            if n:
                pos = struct.unpack_from("<%dH" % n, src, off)
                off += 2 * n
                out += struct.pack(">%dH" % n, *pos)
        assert off == len(src), (i, off, len(src))
        (OUT / f"water{i}.bin").write_bytes(out)

    print("wrote", OUT)
    for f in sorted(OUT.iterdir()):
        print(f"  {f.name}: {f.stat().st_size}")


if __name__ == "__main__":
    main()
