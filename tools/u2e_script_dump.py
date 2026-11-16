import collections

d = open('assets/u2e/u2e_script.bin', 'rb').read()
sp = 0
frame = 0
per_frame = collections.Counter()
first = []
onums = collections.Counter()
maxonum = 0
bad = 0

def lsget(f):
    global sp
    n = f & 3
    v = 0
    for i in range(n):
        b = d[sp]; sp += 1
        if i == n - 1 and b >= 0x80:
            v |= (b - 256) << (8 * i)
        else:
            v |= b << (8 * i)
    return v

while sp < len(d) and frame < 5:
    a = d[sp]; sp += 1
    if a == 0xff:
        a = d[sp]; sp += 1
        if a <= 0x7f:
            frame += 1
            first.append('--- frame %d (fov %d) ---' % (frame, a))
            continue
        if a == 0xff:
            first.append('END')
            break
    if (a & 0xc0) == 0xc0:
        onum = (a & 0x3f) << 4
        a = d[sp]; sp += 1
    else:
        onum = 0
    onum = (onum & 0xff0) | (a & 0xf)
    onums[onum] += 1
    maxonum = max(maxonum, onum)
    sel = (a & 0x30) >> 4
    pflag = 0
    if sel == 1:
        pflag = d[sp]; sp += 1
    elif sel == 2:
        pflag = d[sp] | (d[sp + 1] << 8); sp += 2
    elif sel == 3:
        pflag = d[sp] | (d[sp + 1] << 8) | (d[sp + 2] << 16); sp += 3
    x = lsget(pflag)
    y = lsget(pflag >> 2)
    z = lsget(pflag >> 4)
    m = []
    if pflag & 0x40:
        for b in range(9):
            if pflag & (0x80 << b):
                m.append(lsget(2))
    else:
        for b in range(9):
            if pflag & (0x80 << b):
                m.append(lsget(1))
    if len(first) < 60:
        first.append('obj %2d %s pfl=%06X xyz=(%d,%d,%d) m=%s' % (
            onum, 'ON ' if (a & 0xc0) == 0x80 else ('OFF' if (a & 0xc0) == 0x40 else '   '),
            pflag, x, y, z, m))

print('\n'.join(first))
print()
print('max onum', maxonum, 'total cmds', sum(onums.values()))
print('onum histogram top', onums.most_common(10))
