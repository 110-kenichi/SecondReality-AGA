import struct
from collections import Counter

d = open('assets/u2e/u2e_obj.bin', 'rb').read()
p = 0
n = struct.unpack_from('>H', d, p)[0]; p += 2
print('objects', n)
names_hint = {}
for i in range(n):
    vnum, nnum, tnum, center = struct.unpack_from('>HHHH', d, p); p += 8
    vd = p; p += vnum * 12
    nd = p; p += nnum * 6
    td = p
    flags_c = Counter()
    nrm_bad = 0
    for t in range(tnum):
        a, b, c, nrm, color, flags = struct.unpack_from('>HHHHBB', d, td + t * 10)
        flags_c[flags] += 1
        if nrm >= nnum:
            nrm_bad += 1
    p += tnum * 10
    print('obj %2d v=%3d n=%3d tri=%3d center=%3d flags=%s nrm_oob=%d'
          % (i, vnum, nnum, tnum, center, dict(flags_c), nrm_bad))
