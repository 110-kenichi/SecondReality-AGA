"""OMF オブジェクト内の "uh" 形式画像 → IMG1 コンバータ

Second Reality の画像データ (lbm2p で作られた RLE 形式、OMF に埋め込み) を
Amiga 側で使える IMG1 形式へ変換する。

OMF (Intel) の注意点:
    - レコード長フィールドはチェックサム 1 バイトを含む
      (payload = length - 1 バイト、次のレコードは 3 + length 先)

uh 形式 (x86 リトルエンディアン、16bit int):
    +0x00: u16 magic, u16 wid, u16 hig, u16 cols, u16 add
    +0x10: パレット (cols*3, VGA 6bit)
    +add*16: 行データ列 (各行: u16 長 + RLE データ)
    RLE: 0x80|n = 次の 1 バイトを n 回、それ以外 = リテラル 1 バイト

Usage: python uh2raw.py <input.obj> <output.raw> [--out-height H] [--seg-index N]
"""

import struct
import sys

IMG_HDR = 0x10
PAL_OFF = IMG_HDR
DATA_OFF = PAL_OFF + 256 * 3


def omf_records(d):
    """OMF レコードを (type, payload) で列挙 (length はチェックサム込み)"""
    pos = 0
    while pos + 3 <= len(d):
        t = d[pos]
        size = struct.unpack_from('<H', d, pos + 1)[0]
        if size == 0:
            break
        payload = d[pos + 3:pos + 3 + size - 1]
        yield t, payload
        pos += 3 + size


def extract_segments(d):
    """LEDATA (0xA0/0xA1) を集めてセグメントごとのデータを作る"""
    segs = {}
    for t, p in omf_records(d):
        if t in (0xA0, 0xA1) and len(p) >= 3:
            if t == 0xA1:
                seg = p[0]
                off = struct.unpack_from('<I', p, 1)[0]
                data = p[5:]
            else:
                seg = p[0]
                off = struct.unpack_from('<H', p, 1)[0]
                data = p[3:]
            buf = segs.setdefault(seg, bytearray())
            if len(buf) < off + len(data):
                buf.extend(b'\0' * (off + len(data) - len(buf)))
            buf[off:off + len(data)] = data
    return segs


def decode_rle(src, count):
    out = bytearray()
    i = 0
    while i < len(src) and len(out) < count:
        b = src[i]
        i += 1
        if b & 0x80:
            n = b & 0x7F
            if i < len(src):
                out += bytes([src[i]]) * n
                i += 1
        else:
            out.append(b)
    return out


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
    segs = extract_segments(d)
    print('segments:', {k: len(v) for k, v in segs.items()})
    idx = opts.get('seg-index', max(segs, key=lambda k: len(segs[k])))
    data = bytes(segs[idx])
    print(f'using segment {idx}: {len(data)} bytes')

    magic, wid, hig, cols, add = struct.unpack_from('<HHHHH', data, 0)
    print(f'uh: magic=0x{magic:04X} {wid}x{hig} cols={cols} add={add}')

    pal = data[0x10:0x10 + cols * 3]

    # 行データ (add*16 から、各行 u16 長 + RLE)
    base = add * 16
    rows = []
    off = base
    for y in range(hig):
        ln = struct.unpack_from('<H', data, off)[0]
        rows.append(decode_rle(data[off + 2:off + 2 + ln], wid))
        off += 2 + ln

    out_h = opts.get('out-height', 256)

    # IMG1 出力 (8 プレーン、縦 nearest スケール)
    planes = []
    rowbytes = ((wid + 15) // 16) * 2
    for p in range(8):
        buf = bytearray()
        for oy in range(out_h):
            sy = oy * hig // out_h
            row = rows[sy]
            for bx in range(rowbytes):
                b = 0
                for bit in range(8):
                    x = bx * 8 + bit
                    if x < wid and ((row[x] >> p) & 1):
                        b |= 0x80 >> bit
                buf.append(b)
        planes.append(bytes(buf))

    out = bytearray()
    out += b'IMG1'
    out += struct.pack('>HHBB', wid, out_h, 8, 0)
    out += struct.pack('>H', min(cols, 256))
    out += b'\0' * (PAL_OFF - len(out))
    pal256 = bytearray(256 * 3)
    for i in range(min(cols, 256)):
        pal256[i * 3 + 0] = min(255, pal[i * 3 + 0] << 2)
        pal256[i * 3 + 1] = min(255, pal[i * 3 + 1] << 2)
        pal256[i * 3 + 2] = min(255, pal[i * 3 + 2] << 2)
    out += pal256
    for p in range(8):
        out += planes[p]

    open(args[1], 'wb').write(out)
    print(f'wrote {args[1]}: {len(out)} bytes ({wid}x{out_h} x 8 planes)')


if __name__ == '__main__':
    main()
