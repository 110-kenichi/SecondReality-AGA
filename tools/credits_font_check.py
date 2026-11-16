import re
import numpy as np

lines = open(r'reference/SecondReality/CREDITS/FONA.INC', encoding='latin-1').read().splitlines()
vals = []
for l in lines:
    t = l.split(';')[0].strip()
    if t.lower().startswith('db'):
        vals.extend(int(x) for x in re.findall(r'\d+', t[2:]))

f = np.array(vals, dtype=np.uint8)[:32 * 1500].reshape(32, 1500)
col = (f > 0).any(axis=0)
segs = []
inrun = False
s = 0
for i, c in enumerate(list(col) + [False]):
    if c and not inrun:
        s = i
        inrun = True
    elif (not c) and inrun:
        segs.append((s, i - s))
        inrun = False

order = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?!:,."()+-'
w = {}
for (st, wd), ch in zip(segs, order):
    w[ch] = wd

print('segments', len(segs))
print('A..E widths', [w.get(c, -1) for c in 'ABCDE'])
print('first 10 segs', segs[:10])


def tw(s):
    return sum(w.get(c, 8) + 2 for c in s)


for s in ('GRAPHICS - MARVEL', 'GRAPHICS - PIXEL, SKAVEN', 'RENDERING - TRUG',
          'MUSIC - PURPLE MOTION', 'CODE - WILDFIRE'):
    print('%-24s width %d' % (s, tw(s)))
