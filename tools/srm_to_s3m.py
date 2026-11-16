"""Convert our internal SRM1 module back to a standard S3M file.

The point is to render the *same data* with a known-correct replayer
(libopenmpt / openmpt123) so we can compare the effect semantics
(tempo, Gxx, Dxy, ...) of our Amiga player against a reference.

Usage: python srm_to_s3m.py <in.srm> <out.s3m> [tempo]
"""
import struct
import sys

MAGIC = 0x53524D31  # 'SRM1'


def read_srm(path):
    d = open(path, 'rb').read()
    (magic, nord, nins, npat, nch, speed, tempo, mvol,
     _pad, order_off, ins_off, pattbl_off, _smp) = struct.unpack_from(
        '>IHHHBBBBHIIII', d, 0)
    assert magic == MAGIC, 'not an SRM1 module'
    assert nch == 8
    mod = dict(data=d, nord=nord, nins=nins, npat=npat, speed=speed,
               tempo=tempo, mvol=mvol,
               orders=list(d[order_off:order_off + nord]),
               pattbl=list(struct.unpack_from('>%dI' % npat, d, pattbl_off)))
    instruments = []
    for i in range(nins):
        o = ins_off + i * 48
        name = d[o:o + 28].split(b'\0')[0]
        vol, flags = d[o + 28], d[o + 29]
        c2spd = struct.unpack_from('>H', d, o + 30)[0]
        length, lstart, lend, doff = struct.unpack_from('>IIII', d, o + 32)
        instruments.append(dict(name=name, vol=vol, flags=flags, c2spd=c2spd,
                                length=length, lstart=lstart, lend=lend,
                                doff=doff))
    mod['instruments'] = instruments

    patterns = []
    for p in range(npat):
        base = mod['pattbl'][p]
        out = bytearray()
        for r in range(64):
            for c in range(nch):
                o = base + (r * nch + c) * 5
                note, ins, vol, cmd, info = d[o:o + 5]
                flg = 0
                body = bytearray()
                if note != 0xFF:
                    flg |= 1
                    body.append(note)
                if ins:
                    flg |= 2
                    body.append(ins)
                if vol != 0xFF:
                    flg |= 4
                    body.append(vol)
                if cmd:
                    flg |= 8
                    body.append(cmd)
                    flg |= 16
                    body.append(info)
                out.append(flg)
                out += body
        patterns.append(bytes(out))
    mod['patterns'] = patterns
    return mod


def build_s3m(mod, tempo_override=None):
    nord, nins, npat = mod['nord'], mod['nins'], mod['npat']
    tempo = mod['tempo'] if tempo_override is None else tempo_override
    instruments = mod['instruments']

    # layout: header | orders | ins pp | pat pp | instruments | patterns | samples
    order_off = 0x60
    inspp_off = order_off + nord
    patpp_off = inspp_off + 2 * nins
    pos = (patpp_off + 2 * npat + 15) & ~15
    ins_offs = []
    for _ in range(nins):
        ins_offs.append(pos)
        pos += 80
    pos = (pos + 15) & ~15
    pat_offs = []
    for p in mod['patterns']:
        pat_offs.append(pos)
        pos += 2 + len(p)
        pos = (pos + 15) & ~15
    sample_pos = []
    samples = bytearray()
    for ins in instruments:
        sample_pos.append(pos + len(samples))
        raw = mod['data'][ins['doff']:ins['doff'] + ins['length']]
        samples += bytes(b ^ 0x80 for b in raw)     # signed -> unsigned
        if len(samples) & 1:
            samples.append(0x80)

    out = bytearray(pos + len(samples))

    name = b'UnreaL 2 SRM'
    out[0:len(name)] = name
    out[0x1C] = 0x1A
    out[0x1D] = 0x10
    struct.pack_into('<HHHHHH', out, 0x20, nord, nins, npat, 0x0008, 0x1300, 2)
    out[0x2C:0x30] = b'SCRM'
    out[0x30] = 64                      # global volume
    out[0x31] = mod['speed']
    out[0x32] = tempo
    out[0x33] = mod['mvol']
    out[0x34] = 0                       # ultraclick
    out[0x35] = 0                       # default pan flag
    struct.pack_into('<H', out, 0x3E, 0)   # special
    for c in range(32):
        out[0x40 + c] = 0xFF if c >= 8 else (c + 8)

    out[order_off:order_off + nord] = bytes(mod['orders'])

    for i, ins in enumerate(instruments):
        struct.pack_into('<H', out, inspp_off + 2 * i, ins_offs[i] >> 4)
        o = ins_offs[i]
        out[o] = 1
        out[o + 1:o + 13] = b'sample.dat\0\0'
        ptr = sample_pos[i] >> 4          # S3M parapointer (16-byte units)
        out[o + 13] = (ptr >> 16) & 0xFF
        struct.pack_into('<H', out, o + 14, ptr & 0xFFFF)
        struct.pack_into('<I', out, o + 16, ins['length'])
        struct.pack_into('<I', out, o + 20, ins['lstart'])
        struct.pack_into('<I', out, o + 24, ins['lend'])
        out[o + 28] = ins['vol']
        out[o + 30] = 0                 # pack
        out[o + 31] = ins['flags'] & 0x01
        struct.pack_into('<I', out, o + 32, ins['c2spd'])
        out[o + 48:o + 76] = ins['name'].ljust(28, b'\0')[:28]
        out[o + 76:o + 80] = b'SCRS'

    for p, off in enumerate(pat_offs):
        struct.pack_into('<H', out, patpp_off + 2 * p, off >> 4)
        struct.pack_into('<H', out, off, len(mod['patterns'][p]))
        out[off + 2:off + 2 + len(mod['patterns'][p])] = mod['patterns'][p]

    out[pos:pos + len(samples)] = samples
    return bytes(out), tempo


def main():
    src, dst = sys.argv[1], sys.argv[2]
    tempo_override = int(sys.argv[3]) if len(sys.argv) > 3 else None
    mod = read_srm(src)
    data, tempo = build_s3m(mod, tempo_override)
    open(dst, 'wb').write(data)
    print('wrote %s: orders %d ins %d pat %d tempo %d speed %d size %d'
          % (dst, mod['nord'], mod['nins'], mod['npat'], tempo,
             mod['speed'], len(data)))


if __name__ == '__main__':
    main()
