"""Search for all kinds of references to an address in the STMIK segment:
- call/jmp rel16/rel8
- absolute word values (jump tables)

Usage: python omf_refs.py <file.300> <target_hex>
"""
import struct
import sys

sys.path.insert(0, '.')
from omf_stmik import OMF
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

data = open(sys.argv[1], 'rb').read()
target = int(sys.argv[2], 16)
omf = OMF(data)
omf.parse()
img = omf.segments.get(1, b'')

# 1) rel16 call/jmp (E8/E9)
refs = []
for i in range(len(img) - 3):
    if img[i] in (0xE8, 0xE9):
        rel = struct.unpack_from('<h', img, i + 1)[0]
        if i + 3 + rel == target:
            refs.append((i, 'call' if img[i] == 0xE8 else 'jmp', 3))
# 2) rel8 jmp/jcc
for i in range(len(img) - 2):
    b = img[i]
    if b in (0xEB,):
        rel = struct.unpack_from('<b', img, i + 1)[0]
        if i + 2 + rel == target:
            refs.append((i, 'jmp short', 2))
    if 0x70 <= b <= 0x7F:
        rel = struct.unpack_from('<b', img, i + 1)[0]
        if i + 2 + rel == target:
            refs.append((i, 'jcc short', 2))
# 3) word values (tables)
for i in range(len(img) - 2):
    if struct.unpack_from('<H', img, i)[0] == target:
        refs.append((i, 'word', 2))

print(f"references to 0x{target:04X}:")
for addr, kind, size in refs:
    print(f"  0x{addr:04X}: {kind}")
