"""Amiga hunk 実行ファイルの全ハンクに HUNKF_FAST (bit31) を設定する。

AROS の LoadSeg は HUNKF_FAST を解釈して MEMF_FAST からメモリを確保する
(rom/dos/internalloadseg_aos.c)。AmigaOS の LoadSeg はこのビットを無視する
ため、実機でも安全 (チップ RAM にロードされる)。

Usage: python hunk_fast.py <file.exe>
"""
import struct
import sys

HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32 = 0x3EC
HUNK_SYMBOL, HUNK_DEBUG = 0x3F0, 0x3F1
HUNK_END, HUNK_HEADER = 0x3F2, 0x3F3

HUNKF_FAST = 0x80000000
HUNKF_CHIP = 0x40000000


def patch(path):
    d = bytearray(open(path, 'rb').read())
    if struct.unpack_from('>I', d, 0)[0] != HUNK_HEADER:
        raise SystemExit(f'{path}: HUNK_HEADER ではない')

    pos = 4
    while True:                     # resident library names
        n = struct.unpack_from('>I', d, pos)[0]
        pos += 4
        if n == 0:
            break
        pos += n * 4
    tblsize = struct.unpack_from('>I', d, pos)[0]
    pos += 4 + 8 + tblsize * 4      # table_size, first, last, sizes

    patched = 0
    while pos + 4 <= len(d):
        off = pos
        htype = struct.unpack_from('>I', d, pos)[0]
        base = htype & 0x3FFFFFFF
        if base in (HUNK_CODE, HUNK_DATA, HUNK_BSS):
            new = (htype | HUNKF_FAST) & ~HUNKF_CHIP
            if new != htype:
                struct.pack_into('>I', d, off, new)
                patched += 1
            size = struct.unpack_from('>I', d, pos + 4)[0]
            pos += 8 + size * 4
        elif base == HUNK_RELOC32:
            pos += 4
            while True:
                n = struct.unpack_from('>I', d, pos)[0]
                pos += 4
                if n == 0:
                    break
                pos += 4 + n * 4
        elif base == HUNK_SYMBOL:
            pos += 4
            while True:
                n = struct.unpack_from('>I', d, pos)[0]
                pos += 4
                if n == 0:
                    break
                pos += (n + 1) * 4      # name + value
        elif base == HUNK_DEBUG:
            n = struct.unpack_from('>I', d, pos + 4)[0]
            pos += 8 + n * 4
        elif base == HUNK_END:
            pos += 4
        else:
            raise SystemExit(f'{path}: 未知のハンク 0x{htype:08X} @0x{off:X}')

    if patched:
        open(path, 'wb').write(d)
    print(f'{path}: HUNKF_FAST パッチ {patched} ハンク')


if __name__ == '__main__':
    patch(sys.argv[1])
