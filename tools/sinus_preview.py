import math
import numpy as np
from PIL import Image

# ---- palette from COMAN/MAIN.C ----
pal = bytearray(768)
sin1024 = [round(math.sin(i * 2 * math.pi / 1024) * 256) for i in range(1024)]

for a in range(256):
    uc = (223 - a * 22 // 26) * 3
    b = (230 - a) // 4
    b += sin1024[a * 4 & 1023] // 32
    b = max(0, min(63, b))
    pal[uc + 1] = b
    b = (255 - a) // 3
    b = min(63, b)
    pal[uc + 2] = b
    b = a - 220
    if b < 0:
        b = -b
    b = min(40, b)
    b = 40 - b
    pal[uc + 0] = b // 3

for a in range(768 - 16 * 3):
    b = pal[a]
    b = b * 9 // 6
    pal[a] = min(63, b)

for a in range(24):
    uc = (255 - a) * 3
    b = a - 4
    if b < 0:
        b = 0
    pal[uc + 0] = b // 2
    pal[uc + 1] = 0
    pal[uc + 2] = 0

pal[0] = pal[1] = pal[2] = 0


def expand(vbuf):
    b = np.frombuffer(vbuf, np.uint8).reshape(200, 160)
    img = np.zeros((200, 320), dtype=np.uint8)
    for c in range(80):
        img[:, 4 * c:4 * c + 2] = b[:, c:c + 1]
        img[:, 4 * c + 2:4 * c + 4] = b[:, c + 80:c + 81]
    return img


def to_rgb(idx):
    lut = np.zeros((256, 3), dtype=np.uint8)
    for i in range(256):
        lut[i] = (min(255, pal[i * 3] * 255 // 63),
                  min(255, pal[i * 3 + 1] * 255 // 63),
                  min(255, pal[i * 3 + 2] * 255 // 63))
    return lut[idx]


for n in (0, 1):
    idx = expand(open('run/vbuf_%d.bin' % n, 'rb').read())
    rgb = to_rgb(idx)
    Image.fromarray(rgb).save('run/sinus_%d.png' % n)
    g = rgb.mean(axis=2)
    print('frame', n, 'mean', round(float(g.mean()), 1), 'rows with content:',
          int((g.max(axis=1) > 6).sum()))
    chars = ' .:-=+*#%@'
    for y in range(0, 200, 5):
        line = ''
        for x in range(0, 320, 4):
            line += chars[min(9, int(g[y:y + 5, x:x + 4].mean() / 256 * 10))]
        print(line)
