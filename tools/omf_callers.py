"""Find callers of a given address in the STMIK segment and disassemble context.

Usage: python omf_callers.py <file.300> <target_addr_hex>
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

# find all call rel16 (E8 cw) that target `target`
callers = []
for i in range(len(img) - 3):
    if img[i] == 0xE8:
        rel = struct.unpack_from('<h', img, i + 1)[0]
        if i + 3 + rel == target:
            callers.append(i)

print(f"callers of 0x{target:04X}: {len(callers)}")
md = Cs(CS_ARCH_X86, CS_MODE_16)
for c in callers:
    print(f"\n--- caller at 0x{c:04X} ---")
    start = max(0, c - 48)
    for insn in md.disasm(bytes(img[start:c + 3]), start):
        mark = " <<< CALL" if insn.address == c else ""
        print(f"  {insn.address:04X}: {insn.mnemonic} {insn.op_str}{mark}")
