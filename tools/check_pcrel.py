"""Fail the build if the linked ELF still has PC-relative relocations.

elf2hunk turns every relocation into an absolute HUNK_RELOC32, so a leftover
R_68K_PC* (e.g. an LTO tail call `bra.l` into another LTRANS partition) gets
the hunk base added to its displacement at load time and jumps into BSS.
See the -flto-partition=one comment in src/m2/Makefile (2026-11-14).
"""
import subprocess
import sys

elf = sys.argv[1]
out = subprocess.run(["m68k-amiga-elf-readelf", "-rW", elf],
                     capture_output=True, text=True, check=True).stdout
bad = [l for l in out.splitlines() if "R_68K_PC" in l]
if bad:
    print("ERROR: %d PC-relative relocation(s) left in %s; elf2hunk would "
          "corrupt them:" % (len(bad), elf))
    for l in bad[:10]:
        print("  " + l.strip())
    sys.exit(1)
