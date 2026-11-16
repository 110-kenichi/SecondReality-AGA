#!/usr/bin/env python3
"""Workbench のツールアイコン (m2.exe.info) を作る。

OS 1.3/2.x 互換の DiskObject (WBTOOL) と 2 プレーン (4 色) の画像 1 枚。
スタックは 32768 バイト。使い方:
    python tools/make_icon.py out/m2.exe.info
"""
import struct
import sys

W, H, DEPTH = 48, 22, 2

# 5x7 のビットマップで "SR" を 2 倍角で描く
GLYPH = {
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'R': ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
}


def make_pixels():
    px = [[0] * W for _ in range(H)]
    # 枠 (色 1 = 黒) と内側 (色 3 = 青)
    for y in range(H):
        for x in range(W):
            if x in (0, W - 1) or y in (0, H - 1):
                px[y][x] = 1
            else:
                px[y][x] = 3
    # 文字 (色 2 = 白)、2 倍角
    ox, oy = 6, 4
    for ch in "SR":
        g = GLYPH[ch]
        for gy, row in enumerate(g):
            for gx, c in enumerate(row):
                if c == '1':
                    for dy in range(2):
                        for dx in range(3):
                            px[oy + gy * 2 + dy][ox + gx * 3 + dx] = 2
        ox += 6 * 3
    return px


def planes(px):
    rowbytes = ((W + 15) // 16) * 2
    out = bytearray()
    for p in range(DEPTH):
        for y in range(H):
            row = bytearray(rowbytes)
            for x in range(W):
                if (px[y][x] >> p) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += row
    return bytes(out)


def main(path):
    tooltypes = [b"(Second Reality A1200 port)"]
    gadget = struct.pack(
        ">IhhhhHHHIIIIIHI",
        0,          # NextGadget
        0, 0,       # LeftEdge, TopEdge
        W, H,       # Width, Height
        0x0004,     # Flags: GFLG_GADGIMAGE (GADGHCOMP)
        0x0003,     # Activation: RELVERIFY | GADGIMMEDIATE
        0x0001,     # GadgetType: BOOLGADGET
        1,          # GadgetRender (non-zero = image follows)
        0,          # SelectRender
        0,          # GadgetText
        0,          # MutualExclude
        0,          # SpecialInfo
        0,          # GadgetID
        1,          # UserData: 1 = WB 2.x revision
    )
    assert len(gadget) == 44
    dobj = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi",
        3, 0,               # do_Type = WBTOOL, pad
        0,                  # do_DefaultTool
        1,                  # do_ToolTypes (non-zero = list follows)
        -0x80000000,        # do_CurrentX = NO_ICON_POSITION
        -0x80000000,        # do_CurrentY
        0,                  # do_DrawerData
        0,                  # do_ToolWindow
        32768,              # do_StackSize
    )
    assert len(dobj) == 78, len(dobj)
    image = struct.pack(">hhhhhIBBI", 0, 0, W, H, DEPTH, 1, 0x03, 0x00, 0)
    assert len(image) == 20
    data = planes(make_pixels())
    tt = struct.pack(">I", (len(tooltypes) + 1) * 4)
    for t in tooltypes:
        s = t + b"\0"
        tt += struct.pack(">I", len(s)) + s
    with open(path, "wb") as f:
        f.write(dobj + image + data + tt)
    print("wrote", path)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "m2.exe.info")
