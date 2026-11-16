"""Byte-level diff between the original (repo) S3M and the standard S3M version.
Reports differing regions and their sizes.

Usage: python s3m_diff.py <original.s3m> <standard.s3m>
"""
import sys

a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
print(f"orig={len(a)} bytes, std={len(b)} bytes")

n = min(len(a), len(b))
diffs = []
i = 0
while i < n:
    if a[i] != b[i]:
        start = i
        while i < n and a[i] != b[i]:
            i += 1
        diffs.append((start, i))
    else:
        i += 1

print(f"diff regions: {len(diffs)}")
total = sum(e - s for s, e in diffs)
print(f"total differing bytes: {total} / {n}")
for s, e in diffs[:40]:
    print(f"  0x{s:06X}-0x{e:06X} ({e - s} bytes)")
    sa = a[s:min(e, s + 16)]
    sb = b[s:min(e, s + 16)]
    print(f"    orig: {' '.join(f'{x:02X}' for x in sa)}")
    print(f"    std : {' '.join(f'{x:02X}' for x in sb)}")
if len(diffs) > 40:
    print(f"  ... and {len(diffs) - 40} more regions")
