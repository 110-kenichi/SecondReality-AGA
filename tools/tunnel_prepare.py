#!/usr/bin/env python3
"""TUNNELI の pcalc テーブルを Amiga 用バイナリへ変換する。

reference/SecondReality/TUNNELI/TUNNEL.DAT は Turbo Pascal の
    pcalc : array[0..137, 0..63] of record x, y : integer end
をそのままダンプしたもの (138*64*4 = 35328 バイト, リトルエンディアン)。

Amiga はビッグエンディアンなので int16 を BE に直して
assets/tunnel/pcalc.bin へ書き出す。
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SRC = os.path.join(PROJ, "reference", "SecondReality", "TUNNELI", "TUNNEL.DAT")
DST = os.path.join(PROJ, "assets", "tunnel")

ROWS = 138
COLS = 64


def main():
    data = open(SRC, "rb").read()
    if len(data) != ROWS * COLS * 4:
        sys.exit("unexpected TUNNEL.DAT size: %d" % len(data))

    vals = struct.unpack("<%dh" % (len(data) // 2), data)
    os.makedirs(DST, exist_ok=True)
    open(os.path.join(DST, "pcalc.bin"), "wb").write(
        struct.pack(">%dh" % len(vals), *vals))

    # 参考情報: 各半径インデックスの x/y 範囲
    for r in (0, 25, 50, 80, 100, 133, 137):
        base = r * COLS * 2
        xs = vals[base:base + COLS * 2:2]
        ys = vals[base + 1:base + COLS * 2:2]
        print("pcalc[%3d]: x %4d..%4d  y %4d..%4d" %
              (r, min(xs), max(xs), min(ys), max(ys)))
    print("written to", os.path.join(DST, "pcalc.bin"))


if __name__ == "__main__":
    main()
