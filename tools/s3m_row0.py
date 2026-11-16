"""Dump row 0 of every pattern (standard S3M) to look for marker-like content.

Usage: python s3m_row0.py <standard.s3m>
"""
import struct
import sys

d = open(sys.argv[1], 'rb').read()
ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))


def cells_of_pattern(p):
    off = pat_pp[p] * 16
    if off == 0:
        return []
    ptr = off + 2
    rows = []
    row = []
    r = 0
    while r < 64:
        b = d[ptr]; ptr += 1
        if b == 0:
            rows.append(row)
            row = []
            r += 1
            continue
        ch = b & 0x1F
        c = [ch, None, None, None, None, None]
        if b & 0x20:
            c[1] = d[ptr]; c[2] = d[ptr + 1]; ptr += 2
        if b & 0x40:
            c[3] = d[ptr]; ptr += 1
        if b & 0x80:
            c[4] = d[ptr]; c[5] = d[ptr + 1]; ptr += 2
        row.append(c)
    return rows


for p in range(patnum):
    rows = cells_of_pattern(p)
    if not rows:
        print(f"pat {p:2d}: (empty)")
        continue
    r0 = rows[0]
    desc = []
    for c in r0:
        ch, note, ins, vol, cmd, info = c
        if note is not None or cmd is not None or vol is not None:
            desc.append(f"ch{ch}:n={note} i={ins} v={vol} cmd={cmd} inf={info}")
    n = len(r0)
    print(f"pat {p:2d}: {n:2d} cells | " + " | ".join(desc))
