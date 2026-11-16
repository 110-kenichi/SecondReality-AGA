"""Extract and decrypt LNS&ZOOM.EXE from the shipped SECOND.EXE.

The released demo ships as SECOND.EXE: U2.EXE with all part executables
appended.  Each appended file is obfuscated by MAIN/PACK.C's crypt():
16-bit running key, XOR 0xff while key < 0xf000.  EXE payloads have their
first two bytes replaced by EB FA (jump $) before the crypt.

Usage:
    python tools/extract_second.py <SECOND.EXE> [outdir]

Then unpack the result (two layers, PKLITE outer + EXEPACK inner):

    unp --extract LNS&ZOOM.EXE          -> PKLITE layer
    unp --extract LNS&ZOOM_out.bin      -> EXEPACK layer

The final image contains the LENS picture (320x200, 0..63) at
EXB+784; tools/lens_prepare.py expects it as LENSPIC.FINAL.
"""
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PACKING = HERE.parent / "reference/SecondReality/MAIN/PACKING.INC"
TARGET = "LNS&ZOOM.EXE"


def crypt(buf: bytes, pos: int, adr: int) -> bytes:
    out = bytearray(buf)
    c = (pos * adr) & 0xFFFF
    for i in range(len(out)):
        if c < 0xF000:
            out[i] ^= 0xFF
        c = (c + adr) & 0xFFFF
    return bytes(out)


def load_entries(path: Path):
    entries = []
    pat = re.compile(
        r"\s*dw\s+0([0-9A-Fa-f]+)h,0([0-9A-Fa-f]+)h,0([0-9A-Fa-f]+)h,"
        r"0([0-9A-Fa-f]+)h,0([0-9A-Fa-f]+)h,0([0-9A-Fa-f]+)h\s*;(\S+)"
    )
    for line in path.read_text().splitlines():
        m = pat.match(line)
        if not m:
            continue
        a, b, c, d, e, f, name = m.groups()
        fpos = (int(c, 16) << 16) | int(a, 16)
        adr = int(b, 16) + int(d, 16)
        size = (int(f, 16) << 16) | int(e, 16)
        entries.append((name, fpos, size, adr))
    return entries


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    src = Path(sys.argv[1])
    outdir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(".")
    outdir.mkdir(parents=True, exist_ok=True)

    data = src.read_bytes()
    pos0 = struct.unpack_from("<I", data, len(data) - 4)[0] ^ 0x12345678
    if data[pos0:pos0 + 2] != b"\xC0\x0F":
        print("0x0FC0 marker not found at pos0=0x%X" % pos0)
        return 1

    for name, fpos, size, adr in load_entries(PACKING):
        if name.upper() != TARGET.upper():
            continue
        off = pos0 + 2 + fpos
        payload = crypt(data[off:off + size], off, adr)
        if payload[:2] == b"\xEB\xFA":
            payload = b"MZ" + payload[2:]
        out = outdir / "LNS&ZOOM.EXE"
        out.write_bytes(payload)
        print("wrote %s (%d bytes, fpos=0x%X adr=0x%04X)" % (out, len(payload), fpos, adr))
        return 0

    print("%s not found in PACKING.INC" % TARGET)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
