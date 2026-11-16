#!/usr/bin/env python3
"""PAM (PRAX4.FLI) アセットの変換。

reference/SecondReality/PAM/ の
  - OUT.ANI : ANIM.C が PRAX4.FLI をデコードして作った差分パック形式
  - PAL.PAL : 6bit (0..63) の 256 色パレット
を Amiga 用に変換する。

OUT.ANI の形式 (ANIM.C の packframe 準拠):
  各フレームは 16 バイト境界から始まり、次のバイト列が並ぶ。
    0x01..0x7F : 直後の 1 バイトをその回数だけ書く
    0x80..0xFF : (256 - 値) ピクセルを読み飛ばす
    0x00       : フレーム終端
  フレームは直前のフレームからの差分。

使い方: python tools/pam_prepare.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "reference", "SecondReality", "PAM")
OUT = os.path.join(ROOT, "assets", "pam")


def main():
    ani = open(os.path.join(SRC, "OUT.ANI"), "rb").read()
    pal = open(os.path.join(SRC, "PAL.PAL"), "rb").read()
    if len(pal) != 768:
        sys.exit("PAL.PAL のサイズが不正: %d" % len(pal))

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "pam.ani"), "wb") as f:
        f.write(ani)
    # 6bit -> 8bit (VGA の 0..63 を 0..252 へ)
    pal8 = bytes(min(255, v * 4) for v in pal)
    with open(os.path.join(OUT, "pam.pal"), "wb") as f:
        f.write(pal8)

    # フレーム数を数える (検証用)
    off = 0
    frames = 0
    n = len(ani)
    while off < n:
        while off % 16 and off < n:
            off += 1
        if off >= n:
            break
        while off < n:
            c = ani[off]
            off += 1
            if c == 0:
                break
            if c < 128:
                off += 1
            else:
                pass
        frames += 1
    print("pam.ani %d bytes, %d frames" % (len(ani), frames))
    print("pam.pal 768 bytes (8bit)")


if __name__ == "__main__":
    main()
