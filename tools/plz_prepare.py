#!/usr/bin/env python3
"""PLZPART (plasma) の正弦テーブルをバイナリへ変換する。

reference/SecondReality/PLZPART/ の
    PSINI.INC   (db  16384 個)
    LSINI4.INC  (dw   8192 個)
    LSINI16.INC (dw   8192 個)
を読み、Amiga 側でそのまま使える形で assets/plasma/ へ書き出す。

  psini.bin   16384 bytes  (符号なし 8bit)
  lsini4.bin  16384 bytes  (ビッグエンディアン 16bit x 8192 / Amiga 用)
  lsini16.bin 16384 bytes  (ビッグエンディアン 16bit x 8192 / Amiga 用)
"""

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SRC = os.path.join(PROJ, "reference", "SecondReality", "PLZPART")
DST = os.path.join(PROJ, "assets", "plasma")

NUM = re.compile(rb"-?\d+")


def parse(path):
    """INC ファイルから数値だけを順に取り出す。"""
    data = open(path, "rb").read()
    return [int(x) for x in NUM.findall(data)]


def main():
    os.makedirs(DST, exist_ok=True)

    psini = parse(os.path.join(SRC, "PSINI.INC"))
    lsini4 = parse(os.path.join(SRC, "LSINI4.INC"))
    lsini16 = parse(os.path.join(SRC, "LSINI16.INC"))

    if len(psini) != 16384:
        sys.exit("PSINI.INC: expected 16384 values, got %d" % len(psini))
    if len(lsini4) != 8192:
        sys.exit("LSINI4.INC: expected 8192 values, got %d" % len(lsini4))
    if len(lsini16) != 8192:
        sys.exit("LSINI16.INC: expected 8192 values, got %d" % len(lsini16))

    if max(psini) > 255 or min(psini) < 0:
        sys.exit("PSINI.INC: value out of 8bit range")
    for name, tab in (("LSINI4", lsini4), ("LSINI16", lsini16)):
        if max(tab) > 65535 or min(tab) < 0:
            sys.exit("%s: value out of 16bit range" % name)

    open(os.path.join(DST, "psini.bin"), "wb").write(bytes(psini))
    open(os.path.join(DST, "lsini4.bin"), "wb").write(
        struct.pack(">%dH" % len(lsini4), *lsini4))
    open(os.path.join(DST, "lsini16.bin"), "wb").write(
        struct.pack(">%dH" % len(lsini16), *lsini16))

    print("psini  : %d values, range %d..%d" % (len(psini), min(psini), max(psini)))
    print("lsini4 : %d values, range %d..%d" % (len(lsini4), min(lsini4), max(lsini4)))
    print("lsini16: %d values, range %d..%d" % (len(lsini16), min(lsini16), max(lsini16)))
    print("written to", DST)


if __name__ == "__main__":
    main()
