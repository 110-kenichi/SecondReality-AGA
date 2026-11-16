import struct

def chunks(path):
    d = open(path, 'rb').read()
    off = 0
    while off + 8 <= len(d):
        tag = d[off:off + 4]
        ln = struct.unpack('<I', d[off + 4:off + 8])[0]
        body = d[off + 8:off + 8 + ln]
        yield tag, body
        if tag == b'END ':
            break
        off += 8 + ln

for name in ('U2E.001', 'U2E.009'):
    print('==', name)
    poly = None
    ords = []
    for tag, body in chunks('reference/SecondReality/MAIN/DATA/' + name):
        if tag == b'POLY':
            poly = body
            # count polygon records
            p = 2
            polys = []
            while p + 6 <= len(body):
                n = body[p]
                if n == 0:
                    break
                polys.append((p, n, body[p + 2]))
                p += 6 + 2 * n
            print('  POLY bytes', len(body), 'records', len(polys),
                  'offsets', [x[0] for x in polys])
        elif tag.startswith(b'ORD'):
            words = struct.unpack('<%dH' % (len(body) // 2), body)
            ords.append((tag, words))
    for tag, w in ords:
        print('  %s count=%d sortv=%d polys=%s tail=%d' %
              (tag.decode(), w[0], w[1], list(w[2:-1]), w[-1]))
