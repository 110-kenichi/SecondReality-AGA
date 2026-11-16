"""Extract the XOR key stream (orig ^ std) for all pattern regions and analyze its structure.

Checks:
- Is the key the same for different patterns at the same pattern-relative offset?
- Print the key table.

Usage: python s3m_key.py <original.s3m> <standard.s3m>
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
_, pp2 = patinfo(b)

keys = {}   # off -> set of key values
for p in range(patnum):
    off = pp[p] * 16
    if off == 0:
        continue
    ln = struct.unpack_from('<H', a, off)[0]
    for i in range(ln):
        k = a[off + i] ^ b[off + i]
        keys.setdefault(i, set()).add(k)

# Report offsets where the key differs between patterns
conflicts = {i: ks for i, ks in keys.items() if len(ks) > 1}
print(f"offsets with key conflicts: {len(conflicts)} / {len(keys)}")
for i in sorted(conflicts)[:20]:
    print(f"  off {i}: keys {sorted(hex(k) for k in conflicts[i])}")

# Print the dominant key table for the first 300 offsets
print("\nkey table (offset: key) - dominant values:")
row = []
for i in range(0, 300):
    ks = keys.get(i)
    if ks is None:
        row.append("--")
    elif len(ks) == 1:
        row.append(f"{next(iter(ks)):02X}")
    else:
        row.append(f"*{sorted(ks)[0]:02X}")
for i in range(0, len(row), 32):
    print(f"off {i:3d}: " + " ".join(row[i:i + 32]))
