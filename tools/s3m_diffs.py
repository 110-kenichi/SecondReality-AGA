"""Analyze the 188 remaining byte differences: map to (pattern, offset-in-pattern).

Usage: python s3m_diffs.py <decoded.s3m> <standard.s3m>
"""
import struct
import sys

a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()


def patinfo(d):
    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))
    return patnum, pat_pp


patnum, pp = patinfo(a)
starts = {}
for p in range(patnum):
    off = pp[p] * 16
    if off:
        starts[off] = p
sorted_starts = sorted(starts.items())

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

print(f"{len(diffs)} diff regions")
for s, e in diffs[:60]:
    # find enclosing pattern
    pat = None
    for (off, p) in sorted_starts:
        if off <= s:
            pat = (p, off)
        else:
            break
    if pat:
        p, off = pat
        ln = struct.unpack_from('<H', a, off)[0]
        rel = s - off
        print(f"  0x{s:06X}: pat {p:2d} rel {rel:4d} (len {ln}) dec={a[s]:02X} std={b[s]:02X}")
    else:
        print(f"  0x{s:06X}: (no pattern) dec={a[s]:02X} std={b[s]:02X}")
