"""List all A (set speed) commands with their info values.

Usage: python s3m_acmds.py <standard.s3m>
"""
import struct
import sys

d = open(sys.argv[1], 'rb').read()
ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))

from collections import Counter
vals = Counter()
rows = []
for p in range(patnum):
    off = pat_pp[p] * 16
    if off == 0:
        continue
    ptr = off + 2
    row = 0
    while row < 64:
        b = d[ptr]; ptr += 1
        if b == 0:
            row += 1
            continue
        ch = b & 0x1F
        if b & 0x20:
            ptr += 2
        if b & 0x40:
            ptr += 1
        if b & 0x80:
            cmd = d[ptr]; info = d[ptr + 1]; ptr += 2
            if cmd == 1:  # A
                vals[info] += 1
                rows.append((p, row, ch, info))

print("A command info values:", dict(sorted(vals.items())))
print(f"total: {sum(vals.values())}")
print("first 40 occurrences (pat, row, ch, info):")
for r in rows[:40]:
    print(f"  pat={r[0]:2d} row={r[1]:2d} ch={r[2]} info={r[3]} (0x{r[3]:02X})")
