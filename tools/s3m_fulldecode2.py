"""Full decode of the original S3M using the derived key FORMULA (corrected),
then byte-compare with the standard version and look for command-0 cells.

key(i) = (((i & 3) * 5) ^ (i & 0xC)) | (((((i >> 2) & 3) ^ L[i >> 4]) << 4))
L = [0,5,10,15,4,1,14,11,8,13,2,7,12,9,6,3]
Applied to pattern-relative offsets >= 2 (length word stored plain).

Usage: python s3m_fulldecode2.py <original.s3m> <standard.s3m> [--write out.s3m]
"""
import struct
import sys

L = [0, 5, 10, 15, 4, 1, 14, 11, 8, 13, 2, 7, 12, 9, 6, 3]


def key(i):
    i &= 0xFF
    return (((i & 3) * 5) ^ (i & 0xC)) | (((((i >> 2) & 3) ^ L[i >> 4]) & 0xF) << 4)


a = bytearray(open(sys.argv[1], 'rb').read())
b = open(sys.argv[2], 'rb').read()


def patinfo(d):
    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))
    return patnum, pat_pp


patnum, pp = patinfo(a)

for p in range(patnum):
    off = pp[p] * 16
    if off == 0:
        continue
    ln = struct.unpack_from('<H', a, off)[0]
    for i in range(2, ln):     # skip length word
        a[off + i] ^= key(i)

diffs = []
i = 0
n = min(len(a), len(b))
while i < n:
    if a[i] != b[i]:
        s = i
        while i < n and a[i] != b[i]:
            i += 1
        diffs.append((s, i))
    else:
        i += 1
total = sum(e - s for s, e in diffs)
print(f"decoded original vs standard: {len(diffs)} diff regions, {total} differing bytes")
for s, e in diffs[:20]:
    print(f"  0x{s:06X}-0x{e:06X} ({e - s})")
    print(f"    dec: {' '.join(f'{x:02X}' for x in a[s:min(e, s + 24)])}")
    print(f"    std: {' '.join(f'{x:02X}' for x in b[s:min(e, s + 24)])}")

# scan decoded patterns for command 0 cells and other specials
print("\n=== command histogram of decoded original ===")
from collections import Counter
cmd_hist = Counter()
cmd0_cells = []
for p in range(patnum):
    off = pp[p] * 16
    if off == 0:
        continue
    ptr = off + 2
    row = 0
    while row < 64:
        c = a[ptr]; ptr += 1
        if c == 0:
            row += 1
            continue
        ch = c & 0x1F
        note = ins = vol = cmd = info = None
        if c & 0x20:
            note = a[ptr]; ins = a[ptr + 1]; ptr += 2
        if c & 0x40:
            vol = a[ptr]; ptr += 1
        if c & 0x80:
            cmd = a[ptr]; info = a[ptr + 1]; ptr += 2
            cmd_hist[cmd] += 1
            if cmd == 0:
                cmd0_cells.append((p, row, ch, note, ins, vol, cmd, info))
print(f"commands: {dict(sorted(cmd_hist.items()))}")
print(f"cmd-0 cells: {len(cmd0_cells)}")
for c in cmd0_cells[:40]:
    print(f"  pat={c[0]} row={c[1]} ch={c[2]} note={c[3]} ins={c[4]} vol={c[5]} cmd={c[6]} info=0x{c[7]:02X}")

if '--write' in sys.argv:
    out = sys.argv[sys.argv.index('--write') + 1]
    open(out, 'wb').write(bytes(a))
    print(f"wrote {out}")
