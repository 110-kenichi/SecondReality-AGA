"""ILBM/PBM → .raw (IMG1) コンバータ

Second Reality の LBM 資産を Amiga 側で直接使える形式へ変換する。

IMG1 形式 (ビッグエンディアン):
    0x00: 'IMG1'
    0x04: u16 width, u16 height, u8 planes, u8 pad
    0x08: u16 colors
    0x0A: pad (4 bytes)
    0x10: palette[256*3]  (ILBM の 4bit CMAP を v<<4 で 8bit 化)
    0x310: planar data: planes * (width/8 * height) bytes (プレーンメジャー)

Usage:
    python lbm2raw.py <input.lbm> <output.raw> [--chunky] [--out-height H] [--crop-y Y0]

--chunky: BODY が 1 バイト/ピクセル (DPaint の "PBM " 形式) の場合
"""
import struct
import sys

HDR_SIZE = 0x10
PAL_OFF = HDR_SIZE
DATA_OFF = PAL_OFF + 256 * 3


def parse_iff(d):
    assert d[0:4] == b'FORM', 'not an IFF file'
    chunks = {}
    pos = 12
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        size = struct.unpack_from('>I', d, pos + 4)[0]
        chunks[cid] = d[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    return chunks


def decode_byterun1(src, expected):
    """ILBM ByteRun1 展開"""
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
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opts = {}
    for a in sys.argv[1:]:
        if a.startswith('--'):
            k, sep, v = a[2:].partition('=')
            opts[k] = int(v) if sep else 1
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)

    d = open(args[0], 'rb').read()
    ch = parse_iff(d)
    bmhd = ch[b'BMHD']
    w, h, x, y, nplanes, mask, comp, pad, trans, xa, ya, pw, ph = \
        struct.unpack_from('>HHhhBBBBHBBhh', bmhd, 0)
    cmap = ch[b'CMAP']
    body = ch[b'BODY']

    crop_y = opts.get('crop-y', 0)
    out_h = opts.get('out-height', h - crop_y)
    rowbytes = ((w + 15) // 16) * 2
    chunky = opts.get('chunky', 0)

    # BODY 展開
    total = (w * h) if chunky else (rowbytes * h * nplanes)
    raw = decode_byterun1(body, total) if comp == 1 else body

    planes = []
    if chunky:
        # 1 バイト/ピクセル → 8 プレーン (縦は nearest で out_h へ)
        for p in range(nplanes):
            buf = bytearray()
            for oy in range(out_h):
                sy = crop_y + (oy * (h - crop_y) // out_h)
                row = raw[sy * w:(sy + 1) * w]
                for bx in range(rowbytes):
                    b = 0
                    for bit in range(8):
                        x = bx * 8 + bit
                        if x < w and ((row[x] >> p) & 1):
                            b |= 0x80 >> bit
                    buf.append(b)
            planes.append(bytes(buf))
    else:
        # プレーンメジャーに再配置 (crop 対応)
        for p in range(nplanes):
            buf = bytearray()
            for row in range(crop_y, crop_y + out_h):
                off = (row * nplanes + p) * rowbytes
                buf += raw[off:off + rowbytes]
            planes.append(bytes(buf))

    # 出力
    out = bytearray()
    out += b'IMG1'
    out += struct.pack('>HHBB', w, out_h, nplanes, 0)
    ncol = len(cmap) // 3
    out += struct.pack('>H', ncol)
    out += b'\0' * (PAL_OFF - len(out))
    pal = bytearray(256 * 3)
    maxval = max(cmap[:ncol * 3]) if ncol else 0
    shift = 0 if maxval > 15 else 4      # 8bit 値を含む場合はそのまま使う
    for i in range(min(ncol, 256)):
        pal[i * 3 + 0] = min(255, cmap[i * 3 + 0] << shift)
        pal[i * 3 + 1] = min(255, cmap[i * 3 + 1] << shift)
        pal[i * 3 + 2] = min(255, cmap[i * 3 + 2] << shift)
    out += pal
    for p in range(nplanes):
        out += planes[p]

    open(args[1], 'wb').write(out)
    print(f"wrote {args[1]}: {len(out)} bytes "
          f"({w}x{out_h} x {nplanes} planes, {ncol} colors, crop_y={crop_y})")


if __name__ == '__main__':
    main()
