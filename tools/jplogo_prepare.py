"""Decode JPLOGO's ICEKNGDM.UP (320x400 packed picture) for the Amiga port.

Format (see reference/SecondReality/JPLOGO/READP.C):
  0  u16 magic 0xFCFD
  2  u16 width  (320)
  4  u16 height (400)
  6  u16 colors (256)
  8  u16 add    (paragraph offset to the row table; 49 -> 784)
  16 .. palette (colors*3 bytes, 6-bit VGA)
  add*16 .. row records: [u16 rle_size][rle data]
      rle byte < 0x80: literal pixel
      rle byte >= 0x80: repeat next byte (byte & 0x7f) times

Outputs (assets/jplogo):
  jplogo_pic.bin : 320*400 bytes of palette indices
  jplogo_pal.bin : 768 bytes (8-bit RGB, 6bit<<2)
"""
import struct
from pathlib import Path

REF = Path(r"reference/SecondReality/JPLOGO")
OUT = Path(r"assets/jplogo")
OUT.mkdir(parents=True, exist_ok=True)


def decode_rle(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        i += 1
        if b < 0x80:
            out.append(b)
        else:
            cnt = b & 0x7F
            if i >= n:
                break
            out += bytes([data[i]]) * cnt
            i += 1
    return bytes(out)


def main() -> int:
    src = (REF / "ICEKNGDM.UP").read_bytes()
    magic, wid, hig, cols, add = struct.unpack_from("<5H", src, 0)
    assert magic == 0xFCFD, hex(magic)
    assert cols == 256, cols
    pal6 = src[16:16 + cols * 3]
    pal8 = bytes(min(255, v << 2) for v in pal6)

    pos = add * 16
    rows = bytearray()
    for y in range(hig):
        size = struct.unpack_from("<H", src, pos)[0]
        pos += 2
        rle = src[pos:pos + size]
        pos += size
        row = decode_rle(rle)
        if len(row) < wid:
            row = row + bytes(wid - len(row))
        rows += row[:wid]
    assert len(rows) == wid * hig, len(rows)
    assert pos == len(src), (pos, len(src))

    (OUT / "jplogo_pic.bin").write_bytes(rows)
    (OUT / "jplogo_pal.bin").write_bytes(pal8)

    # 元の SIN1024.INC (1024 x s16, 振幅 255)。Amiga 側は BE で読む。
    inc = (REF / "SIN1024.INC").read_text(errors="replace").split("\n", 1)[1]
    import re
    vals = [int(x) for x in re.findall(r"-?\d+", inc)]
    if len(vals) != 1024:
        vals = (vals + [0] * 1024)[:1024]
    (OUT / "jplogo_sin.bin").write_bytes(b"".join(struct.pack(">h", v) for v in vals))

    print("jplogo_pic.bin: %d bytes (%dx%d)" % (len(rows), wid, hig))
    print("jplogo_pal.bin: %d bytes" % len(pal8))
    print("jplogo_sin.bin: %d bytes" % (1024 * 2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
