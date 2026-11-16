"""FOREST (MNTSCRL / Vuori-Scrolli) 用アセット変換

入力 (reference/SecondReality/FOREST/):
  HILLBACK.OBJ : OMF。+10 に 768B の 6bit パレット、+778 に 320x200 の
                 最終背景 (FINAL.LBM と同一のピクセル列)。元 READ.PAS は
                 HILLBACK.LBM ではなくこの OBJ を背景に使う。
  O2.SCI       : +778 に 640x31 のスクロール文字 (READ2.PAS)
  POS1-3.DAT   : 31 レイヤ x 237 色のオフセットリスト (u16 LE)

出力 (assets/forest/):
  forest_bg.bin   : 320*200 = 64000 バイト
  forest_pal.bin  : 256*3 = 768 バイト (8bit RGB)
  forest_mask.bin : 640*31 = 19840 バイト (非ゼロ値に +128)
  forest1.bin .. forest3.bin : リストをビッグエンディアン化
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "reference" / "SecondReality" / "FOREST"
OUT = ROOT / "assets" / "forest"

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from uh2raw import extract_segments


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    # --- 背景 + パレット (HILLBACK.OBJ のセグメント) ---
    obj = (SRC / "HILLBACK.OBJ").read_bytes()
    segs = extract_segments(obj)
    seg = bytes(segs[max(segs, key=lambda k: len(segs[k]))])
    assert len(seg) == 10 + 768 + 64000, len(seg)
    pal6 = seg[10:10 + 768]
    bg = seg[778:778 + 64000]
    pal = bytes(min(255, v << 2) for v in pal6)   # VGA 6bit -> 8bit
    (OUT / "forest_bg.bin").write_bytes(bg)
    (OUT / "forest_pal.bin").write_bytes(pal)

    # --- スクロール文字 (O2.SCI, 640x31) ---
    # 最終版 MNTSCRL は READ2.PAS (FOREST/X.BAT: copy read2.exe ..\main\data\mntscrl.exe)。
    # READ2.PAS:117-125: o2.sci の +778 から 640 バイト x 31 行を fbuf に読み、
    # 非ゼロに +128。237 列の窓 (font) へは forest.c が 1 列ずつ流し込む。
    # 以前は旧版 READ.PAS の O.SCI (237 列を最初から窓に置く) を使っていたため、
    # 文字が奥から流れてこず最初から手前に出ていた (2026-11-15 ユーザー指摘)。
    sci = (SRC / "O2.SCI").read_bytes()
    full = bytearray(640 * 31)
    for row in range(31):
        src = 778 + row * 640
        full[row * 640:row * 640 + 640] = sci[src:src + 640]
    for i in range(len(full)):
        if full[i] > 0:
            full[i] = (full[i] + 128) & 0xFF
    (OUT / "forest_mask.bin").write_bytes(full)

    # --- POS リスト (u16 LE -> BE) ---
    for i in range(1, 4):
        src = (SRC / f"POS{i}.DAT").read_bytes()
        off = 0
        out = bytearray()
        for e in range(31 * 237):
            n = struct.unpack_from("<H", src, off)[0]
            off += 2
            out += struct.pack(">H", n)
            if n:
                pos = struct.unpack_from("<%dH" % n, src, off)
                off += 2 * n
                out += struct.pack(">%dH" % n, *pos)
        assert off == len(src), (i, off, len(src))
        (OUT / f"forest{i}.bin").write_bytes(out)

    print("wrote", OUT)
    for f in sorted(OUT.iterdir()):
        print(f"  {f.name}: {f.stat().st_size}")


if __name__ == "__main__":
    main()
