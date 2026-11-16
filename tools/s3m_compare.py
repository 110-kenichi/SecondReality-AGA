"""Compare original (custom-encoded) pattern data with the standard S3M version.

Usage: python s3m_compare.py <original.s3m> <standard.s3m> [pattern_index]
"""
import struct
import sys


def header(d):
    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    orders = list(d[0x60:0x60 + ordnum])
    ins_pp = list(struct.unpack_from('<%dH' % insnum, d, 0x60 + ordnum))
    pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))
    return ordnum, insnum, patnum, orders, ins_pp, pat_pp


def decode_std(d, off):
    """Decode standard ST3 pattern packing into rows of cells."""
    ptr = off + 2
    rows = []
    row = []
    while len(rows) < 64:
        b = d[ptr]; ptr += 1
        if b == 0:
            rows.append(row)
            row = []
            continue
        ch = b & 0x1F
        cell = {'ch': ch, 'note': None, 'ins': None, 'vol': None, 'cmd': None, 'info': None}
        if b & 0x20:
            cell['note'] = d[ptr]; cell['ins'] = d[ptr + 1]; ptr += 2
        if b & 0x40:
            cell['vol'] = d[ptr]; ptr += 1
        if b & 0x80:
            cell['cmd'] = d[ptr]; cell['info'] = d[ptr + 1]; ptr += 2
        row.append(cell)
    return rows


def main():
    orig = open(sys.argv[1], 'rb').read()
    std = open(sys.argv[2], 'rb').read()
    pat = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    o = header(orig)
    s = header(std)
    print(f"orig: ordnum={o[0]} insnum={o[1]} patnum={o[2]} pat_pp[{pat}]=0x{o[5][pat]*16:X}")
    print(f"std : ordnum={s[0]} insnum={s[1]} patnum={s[2]} pat_pp[{pat}]=0x{s[5][pat]*16:X}")

    o_off = o[5][pat] * 16
    s_off = s[5][pat] * 16
    print(f"\n--- standard decoded (pattern {pat}) ---")
    std_rows = decode_std(std, s_off)
    for r, row in enumerate(std_rows):
        if row:
            print(f"row {r:2d}: " + " | ".join(
                f"ch{c['ch']} n={c['note']} i={c['ins']} v={c['vol']} cmd={c['cmd']} inf={c['info']}"
                for c in row))

    # Raw dump of the original pattern data (guess length: standard pattern length)
    std_len = struct.unpack_from('<H', std, s_off)[0]
    print(f"\n--- original raw bytes (pattern {pat}, first 256) ---")
    raw = orig[o_off:o_off + 256]
    for i in range(0, len(raw), 32):
        chunk = raw[i:i + 32]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        print(f"{o_off + i:06X}: {hexs}")

    # Find where the next pattern starts to know the region size
    nxt = o[5][pat + 1] * 16 if pat + 1 < o[2] else len(orig)
    print(f"\noriginal pattern region: 0x{o_off:X} .. 0x{nxt:X} ({nxt - o_off} bytes)")
    print(f"standard pattern length: {std_len} bytes (packed)")


main()
