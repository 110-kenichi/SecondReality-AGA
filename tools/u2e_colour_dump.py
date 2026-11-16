import struct
from collections import Counter

d = open('assets/u2e/u2e_obj.bin', 'rb').read()
p = 2
n = struct.unpack_from('>H', d, 0)[0]
allc = Counter()
per_obj = []
for i in range(n):
    vnum, nnum, tnum, center = struct.unpack_from('>HHHH', d, p); p += 8
    vd = p; p += vnum * 12
    nd = p; p += nnum * 6
    td = p
    cc = Counter()
    for t in range(tnum):
        a, b, c, nrm, color, flags = struct.unpack_from('>HHHHBB', d, td + t * 10)
        cc[color] += 1
        allc[color] += 1
    p += tnum * 10
    per_obj.append((i, min(cc), max(cc), sorted(cc)[:6]))

print('global colour min/max', min(allc), max(allc))
print('most common', allc.most_common(12))
print('count >= 240:', sum(v for k, v in allc.items() if k >= 240),
      'of', sum(allc.values()))
for o in per_obj[:8]:
    print('obj %2d colour %d..%d sample %s' % o)
