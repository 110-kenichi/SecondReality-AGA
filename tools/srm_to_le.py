"""Convert a big-endian .srm (Amiga) module to a little-endian copy for the
host-side player harness (tools/hostrender.c).

usage: python tools/srm_to_le.py <in.srm> <out.srm>
"""
import struct
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = bytearray(open(src, 'rb').read())
    magic, no, ni, np_, ch, sp, tp, mv, pad = struct.unpack_from('>IHHHBBBBH', d, 0)
    oo, io, po, so = struct.unpack_from('>IIII', d, 16)
    out = bytearray(d)
    struct.pack_into('<IHHHBBBBH', out, 0, magic, no, ni, np_, ch, sp, tp, mv, pad)
    struct.pack_into('<IIII', out, 16, oo, io, po, so)
    # オーダーリストは 1 バイト/個なのでバイトスワップ不要
    for i in range(ni):
        b = io + i * 48
        vol, flags = struct.unpack_from('>BB', d, b + 28)
        c2spd, = struct.unpack_from('>H', d, b + 30)
        ln, ls, le, doff = struct.unpack_from('>IIII', d, b + 32)
        struct.pack_into('<BBHIIII', out, b + 28, vol, flags, c2spd, ln, ls, le, doff)
    for i in range(np_):
        v, = struct.unpack_from('>I', d, po + i * 4)
        struct.pack_into('<I', out, po + i * 4, v)
    open(dst, 'wb').write(out)
    print('wrote', dst, 'orders', no, 'ins', ni, 'pat', np_)


if __name__ == '__main__':
    main()
