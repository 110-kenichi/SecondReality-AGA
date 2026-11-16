"""Decode the original (obfuscated) S3M pattern data and compare with the standard version.

Transformation discovered:
    orig[off] = std[off] XOR key[off]
    key[off] = (K[off & 0xF] + 0x50 * (off >> 4)) & 0xFF
    where off = pattern-relative offset (0 = pattern length word)
    K = [0x00,0x05,0x0A,0x0F,0x14,0x11,0x1E,0x1B,0x28,0x2D,0x22,0x27,0x3C,0x39,0x36,0x33]

Usage: python s3m_decode.py <original.s3m> <standard.s3m>
"""
import struct
import sys

K = [0x00, 0x05, 0x0A, 0x0F, 0x14, 0x11, 0x1E, 0x1B, 0x28, 0x2D, 0x22, 0x27, 0x3C, 0x39, 0x36, 0x33]


def key(off):
    return (K[off & 0xF] + 0x50 * (off >> 4)) & 0xFF


def header(d):
    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))
    return patnum, pat_pp


def main():
    orig = bytearray(open(sys.argv[1], 'rb').read())
    std = open(sys.argv[2], 'rb').read()
    patnum, pat_pp = header(orig)
    patnum2, pat_pp2 = header(std)
    assert patnum == patnum2

    # Build list of pattern data regions (start = length word offset)
    regions = []
    for p in range(patnum):
        off = pat_pp[p] * 16
        if off == 0:
            continue
        ln = struct.unpack_from('<H', orig, off)[0]
        regions.append((off, ln))

    # Decode: for each region, XOR with key(off - region_start)
    decoded = bytearray(orig)
    for (off, ln) in regions:
        for i in range(ln):
            decoded[off + i] ^= key(i)

    # Compare decoded with standard
    diffs = []
    i = 0
    n = len(decoded)
    while i < n:
        if decoded[i] != std[i]:
            s = i
            while i < n and decoded[i] != std[i]:
                i += 1
            diffs.append((s, i))
        else:
            i += 1
    total = sum(e - s for s, e in diffs)
    print(f"pattern regions: {len(regions)}")
    print(f"decoded vs standard: {len(diffs)} diff regions, {total} differing bytes")
    for s, e in diffs[:30]:
        print(f"  0x{s:06X}-0x{e:06X} ({e - s} bytes)")
        da = decoded[s:min(e, s + 24)]
        st = std[s:min(e, s + 24)]
        print(f"    dec: {' '.join(f'{x:02X}' for x in da)}")
        print(f"    std: {' '.join(f'{x:02X}' for x in st)}")
    if len(diffs) > 30:
        print(f"  ... and {len(diffs) - 30} more")


main()
