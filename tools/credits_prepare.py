#!/usr/bin/env python3
"""CREDITS (クレジット) の資産を変換する。

入力:
    reference/cpp/CREDITS_MAIN_Data.cpp (C++ 版 SecondRealityPlusPlus の
        Parts/CREDITS/CREDITS_MAIN_Data.cpp。無ければダウンロードする)
        画像 21 枚はここから取る。リポジトリの PICS/*.LBM は最終版より古く、
        PIC02/05B/09/14B/16/17/18 が最終版 (動画) と別の絵になっている
        (2026-11-15 比較: 7 枚が全画素で不一致)。
        各配列は 16 バイトのヘッダ + 768 (6bit パレット) + 160x100。
    reference/SecondReality/CREDITS/FONA.INC  (32x1500 のクレジットフォント)

出力 (assets/credits/):
    credits_pics.bin   u16 count + count * (768 palette + 16000 chunky)
    credits_font.bin   32 * 1500 bytes
"""
import os
import re
import struct
import sys
import urllib.request

CPP_URL = ('https://raw.githubusercontent.com/XorJS/SecondRealityPlusPlus/'
           'main/Parts/CREDITS/CREDITS_MAIN_Data.cpp')

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SRC = os.path.join(PROJ, 'reference', 'SecondReality', 'CREDITS')
DST = os.path.join(PROJ, 'assets', 'credits')

sys.path.insert(0, HERE)
import lbm2raw

PICS = ['PIC01', 'PIC02', 'PIC03', 'PIC04', 'PIC05', 'PIC05B', 'PIC06',
        'PIC07', 'PIC08', 'PIC09', 'PIC10', 'PIC10B', 'PIC11', 'PIC12',
        'PIC13', 'PIC14', 'PIC14B', 'PIC15', 'PIC16', 'PIC17', 'PIC18']


def load_lbm(path):
    d = open(path, 'rb').read()
    ch = lbm2raw.parse_iff(d)
    bmhd = ch[b'BMHD']
    w, h, x, y, nplanes, mask, comp, pad, trans, xa, ya, pw, ph = \
        struct.unpack_from('>HHhhBBBBHBBhh', bmhd, 0)
    cmap = ch[b'CMAP']
    body = ch[b'BODY']
    rowbytes = ((w + 15) // 16) * 2
    raw = lbm2raw.decode_byterun1(body, rowbytes * h * nplanes) if comp == 1 else body

    img = bytearray(w * h)
    for row in range(h):
        base = row * nplanes * rowbytes
        for bx in range(rowbytes):
            v = 0
            for p in range(nplanes):
                b = raw[base + p * rowbytes + bx]
                if b:
                    for bit in range(8):
                        if b & (0x80 >> bit):
                            v |= 0  # placeholder (per pixel below)
            # per-pixel assembly
            for bit in range(8):
                px = bx * 8 + bit
                if px >= w:
                    break
                c = 0
                for p in range(nplanes):
                    if raw[base + p * rowbytes + bx] & (0x80 >> bit):
                        c |= 1 << p
                img[row * w + px] = c

    pal = bytearray(768)
    ncol = len(cmap) // 3
    maxval = max(cmap[:ncol * 3]) if ncol else 0
    shift = 4 if maxval <= 15 else 0
    for i in range(min(ncol, 256)):
        for k in range(3):
            pal[i * 3 + k] = min(255, cmap[i * 3 + k] << shift) if maxval <= 15 else cmap[i * 3 + k]
    return w, h, bytes(img), bytes(pal)


def load_font():
    vals = []
    for line in open(os.path.join(SRC, 'FONA.INC'), encoding='latin-1'):
        t = line.split(';')[0].strip()
        if t.lower().startswith('db'):
            vals.extend(int(x) for x in re.findall(r'\d+', t[2:]))
    need = 32 * 1500
    if len(vals) < need:
        print('warning: font has only %d values, need %d' % (len(vals), need))
        vals += [0] * (need - len(vals))
    return bytes(vals[:need])


def cpp_path():
    path = os.path.join(PROJ, 'reference', 'cpp', 'CREDITS_MAIN_Data.cpp')
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        urllib.request.urlretrieve(CPP_URL, path)
    return path


def load_cpp_pics():
    src = open(cpp_path(), encoding='latin-1').read()
    pics = []
    for name in PICS:
        var = 'credits_pic' + name[3:].lstrip('0').lower()
        m = re.search(r'\b%s\[\] = \{(.*?)\};' % var, src, re.S)
        assert m, var
        v = [int(x, 0) & 255 for x in re.findall(r'0x[0-9a-fA-F]+|\d+', m.group(1))]
        assert len(v) == 16 + 768 + 16000, (var, len(v))
        assert v[2] == 160 and v[4] == 100, var
        # 6bit VGA パレット -> 8bit (credits.c のグレー階調と同じ v*255/63)
        pal = bytes(min(255, c * 255 // 63) for c in v[16:16 + 768])
        pics.append((pal, bytes(v[784:784 + 16000])))
    return pics


def main():
    os.makedirs(DST, exist_ok=True)
    out = bytearray()
    out += struct.pack('>H', len(PICS))
    for name, (pal, img) in zip(PICS, load_cpp_pics()):
        out += pal
        out += img
        print('%s colours=%d' % (name, sum(1 for i in range(0, 768, 3) if pal[i:i + 3] != b'\0\0\0')))
    open(os.path.join(DST, 'credits_pics.bin'), 'wb').write(bytes(out))
    print('credits_pics.bin', len(out))

    font = load_font()
    open(os.path.join(DST, 'credits_font.bin'), 'wb').write(font)
    print('credits_font.bin', len(font))


if __name__ == '__main__':
    main()
