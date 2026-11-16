"""Amiga hunk ファイル (.exe) のヘッダ情報を表示する。

Usage: python hunk_info.py <file.exe>
"""
import struct
import sys

HUNK_TYPES = {
    0x3E9: 'HUNK_CODE', 0x3EA: 'HUNK_DATA', 0x3EB: 'HUNK_BSS',
    0x3EC: 'HUNK_RELOC32', 0x3ED: 'HUNK_RELOC16', 0x3EE: 'HUNK_RELOC8',
    0x3EF: 'HUNK_EXT', 0x3F0: 'HUNK_SYMBOL', 0x3F1: 'HUNK_DEBUG',
    0x3F2: 'HUNK_END', 0x3F3: 'HUNK_HEADER', 0x3F4: 'HUNK_OVERLAY',
    0x3F5: 'HUNK_BREAK', 0x3F6: 'HUNK_DREL32', 0x3F7: 'HUNK_DREL16',
    0x3F8: 'HUNK_DREL8', 0x3F9: 'HUNK_LIB', 0x3FA: 'HUNK_INDEX',
    0x3FB: 'HUNK_RELOC32SHORT', 0x3FC: 'HUNK_RELRELOC32',
    0x3FD: 'HUNK_ABSRELOC16',
}


def main(path):
    d = open(path, 'rb').read()
    pos = 0

    def rd():
        nonlocal pos
        v = struct.unpack_from('>I', d, pos)[0]
        pos += 4
        return v

    magic = rd()
    print(f"file={path} size={len(d)} (0x{len(d):X})")
    print(f"first long=0x{magic:08X}")
    if magic != 0x3F3:
        print("HUNK_HEADER ではない")
        return
    rd()                      # resident library names
    tblsize = rd()
    first = rd()
    last = rd()
    sizes = [rd() for _ in range(tblsize)]
    print(f"table_size={tblsize} first={first} last={last}")
    print(f"sizes(longs)={sizes}")
    print(f"total hunk data = {sum(sizes) * 4} bytes")

    n = 0
    while pos + 4 <= len(d):
        off = pos
        htype = rd()
        flags = htype & 0xC0000000
        base = htype & 0x3FFFFFFF
        name = HUNK_TYPES.get(base, f'0x{base:X}')
        fl = []
        if flags & 0x40000000:
            fl.append('CHIP')
        if flags & 0x80000000:
            fl.append('FAST')
        print(f"hunk {n}: off=0x{off:X} type=0x{htype:08X} {name} flags={','.join(fl) or '-'}")
        if base in (0x3E9, 0x3EA):
            size = rd()
            print(f"   size={size} longs ({size * 4} bytes)")
            pos += size * 4
        elif base == 0x3EB:
            size = rd()
            print(f"   bss size={size} longs ({size * 4} bytes)")
        elif base == 0x3EC:
            while True:
                cnt = rd()
                if cnt == 0:
                    break
                target = rd()
                pos += cnt * 4
            print("   reloc32 (walked)")
        elif base in (0x3F0, 0x3F1):   # HUNK_SYMBOL / HUNK_DEBUG
            if base == 0x3F0:
                while True:
                    cnt = rd()
                    if cnt == 0:
                        break
                    pos += (cnt + 1) * 4
            else:
                cnt = rd()
                pos += cnt * 4
            print("   symbol/debug block (walked)")
        elif base == 0x3F2:
            pass
        else:
            print("   (未知のハンク種別: ここで停止)")
            break
        n += 1
    print(f"walk ended at 0x{pos:X} / 0x{len(d):X}")


if __name__ == '__main__':
    main(sys.argv[1])
