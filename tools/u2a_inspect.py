import struct, os

BASE = 'reference/SecondReality/MAIN/DATA'

def chunks(fn):
    d = open(os.path.join(BASE, fn), 'rb').read()
    off = 0
    while off + 8 <= len(d):
        tag = d[off:off+4]
        ln = struct.unpack('<I', d[off+4:off+8])[0]
        body = d[off+8:off+8+ln]
        yield tag, body
        if tag == b'END ':
            break
        off += 8 + ln

for fn in ['U2A.001', 'U2A.002', 'U2A.003']:
    print('=====', fn)
    for tag, body in chunks(fn):
        if tag == b'VERT':
            n = struct.unpack('<i', body[:4])[0]
            print(' VERT n=%d datalen=%d (16B each: %d)' % (n, len(body)-4, (len(body)-4)//16))
            for i in range(min(3, n)):
                print('   v%d' % i, struct.unpack('<iiii', body[4+i*16:4+i*16+16]))
        elif tag == b'NORM':
            n, n1 = struct.unpack('<ii', body[:8])
            print(' NORM n=%d n1=%d datalen=%d' % (n, n1, len(body)-8))
            for i in range(min(3, n)):
                print('   n%d' % i, struct.unpack('<hhhh', body[8+i*8:8+i*8+8]))
        elif tag == b'POLY':
            print(' POLY len', len(body))
            p = 2
            cnt = 0
            sides = {}
            while p + 6 <= len(body):
                n = body[p]
                if n == 0:
                    break
                rec = struct.unpack('<4B', body[p:p+4])
                nrm = struct.unpack('<H', body[p+4:p+6])[0]
                vs = struct.unpack('<%dH' % n, body[p+6:p+6+2*n])
                sides[n] = sides.get(n, 0)+1
                if cnt < 4:
                    print('   poly %d sides=%d flags=0x%02x col=%d nrm=%d v=%s' % (cnt, n, rec[1], rec[2], nrm, vs))
                p += 6 + 2*n
                cnt += 1
            print('   total polys', cnt, 'sides', dict(sorted(sides.items())), 'endpos', p, 'len', len(body))
        elif tag == b'ORD0':
            vals = struct.unpack('<%dH' % (len(body)//2), body)
            print(' ORD0 len=%d head=%s' % (len(body), vals[:12]))
        elif tag == b'NAME':
            print(' NAME', body.split(b'\0')[0])
