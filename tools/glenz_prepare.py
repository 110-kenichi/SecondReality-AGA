"""Prepare GLENZ assets from the original Second Reality data.

  reference/SecondReality/GLENZ/FC.UH  (64784 bytes)
      = 768 bytes (unused preamble) + 16 bytes + 320x200 8-bit indices
      The 16-colour palette used by the part is at file offset 16 (48 bytes,
      6-bit VGA values).  MAIN.C does `fc[16 + a]` for the 16*3 entries.

Outputs (assets/glenz/):
  glenz_bg.bin   320*200 bytes, palette indices 0..15
  glenz_pal.bin  256*3 bytes, 8-bit RGB (first 16 = the FC palette,
                 the rest zero; the part overwrites the glenz blocks itself)
  glenz_sin.bin  3600 x s16 big-endian, sin(2*pi*i/3600) * 32767

Usage: python tools/glenz_prepare.py
"""
import math
import os
import struct

SRC = os.path.join('reference', 'SecondReality', 'GLENZ', 'FC.UH')
OUT = os.path.join('assets', 'glenz')


def main():
    d = open(SRC, 'rb').read()
    assert len(d) == 64784, len(d)

    pal6 = d[16:16 + 48]
    img = d[784:784 + 64000]
    assert max(img) <= 15, max(img)

    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, 'glenz_bg.bin'), 'wb').write(img)

    pal = bytearray(256 * 3)
    for i in range(16):
        for c in range(3):
            v = pal6[i * 3 + c] & 63
            pal[i * 3 + c] = (v << 2) | (v >> 4)
    open(os.path.join(OUT, 'glenz_pal.bin'), 'wb').write(bytes(pal))

    sin = bytearray()
    for i in range(3600):
        v = int(round(math.sin(2.0 * math.pi * i / 3600.0) * 32767.0))
        sin += struct.pack('>h', v)
    open(os.path.join(OUT, 'glenz_sin.bin'), 'wb').write(bytes(sin))

    print('wrote %s: bg %d bytes, pal 768, sin %d bytes'
          % (OUT, len(img), len(sin)))
    print('FC palette (6-bit):')
    for i in range(16):
        print('  %2d: %2d %2d %2d' % (i, pal6[i * 3], pal6[i * 3 + 1],
                                      pal6[i * 3 + 2]))


if __name__ == '__main__':
    main()
