"""Analyze the transformation between original and standard pattern data.

Hypothesis tests:
1. orig = std XOR keystream -> check keystream periodicity / offset relation
2. orig = std + delta stream
3. orig = byte permutation / compression

Usage: python s3m_crypt.py <original.s3m> <standard.s3m> [start] [len]
"""
import sys

a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
start = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0xF72
n = int(sys.argv[4]) if len(sys.argv) > 4 else 512

print(f"offset  orig std  xor  sub(orig-std)")
for i in range(n):
    o = a[start + i]
    s = b[start + i]
    if i < 64:
        print(f"{start + i:06X}: {o:02X}  {s:02X}  {o ^ s:02X}  {(o - s) & 0xFF:02X}")

xor = bytes(a[start + i] ^ b[start + i] for i in range(n))
sub = bytes((a[start + i] - b[start + i]) & 0xFF for i in range(n))

# Look for periodicity in the xor stream
print("\nxor stream (first 128):")
print(" ".join(f"{x:02X}" for x in xor[:128]))

for period in range(2, 65):
    ok = all(xor[i] == xor[i - period] for i in range(period, min(n, period * 4 + period)))
    if ok:
        print(f"xor period {period} found!")
        break
else:
    print("no short period in xor stream")

# Check relation to file offset (e.g., key = f(offset) = low byte of offset)
low = bytes((start + i) & 0xFF for i in range(n))
xor2 = bytes(x ^ y for x, y in zip(xor, low))
print("\n(xor ^ offset_lowbyte) first 32:", " ".join(f"{x:02X}" for x in xor2[:32]))

# Check relation to high byte of offset
hi = bytes(((start + i) >> 8) & 0xFF for i in range(n))
xor3 = bytes(x ^ y for x, y in zip(xor, hi))
print("(xor ^ offset_highbyte) first 32:", " ".join(f"{x:02X}" for x in xor3[:32]))
