#!/usr/bin/env python3
"""U2E (Vector Part II - KewlComplex) の資産を Amiga 用バイナリに変換する。

入力 (reference/SecondReality/MAIN/DATA/):
    U2E.00M         palette(16..784) + インスタンス表(784.., 16bit)
    U2E.001..042    オブジェクト (U2A と同じチャンク形式)
    U2E.0AB         シーンスクリプト

出力 (assets/u2e/):
    u2e_pal.bin     768    パレット (8bit に拡張)
    u2e_obj.bin            ユニークオブジェクト
        u16 objcount
        for each: u16 vnum, u16 nnum, u16 tnum, u16 center
                  vnum * (s32 x,y,z)
                  nnum * (s16 x,y,z)
                  tnum * (u16 a, u16 b, u16 c, u16 nrm, u8 color, u8 flags)
    u2e_inst.bin    u16 count + count * u16 (object index)
    u2e_script.bin  U2E.0AB そのまま
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SRC = os.path.join(PROJ, 'reference', 'SecondReality', 'MAIN', 'DATA')
DST = os.path.join(PROJ, 'assets', 'u2e')


def chunks(path):
    d = open(path, 'rb').read()
    off = 0
    while off + 8 <= len(d):
        tag = d[off:off + 4]
        ln = struct.unpack('<I', d[off + 4:off + 8])[0]
        yield tag, d[off + 8:off + 8 + ln]
        if tag == b'END ':
            break
        off += 8 + ln


def load_object(path):
    verts, normals, polys, name, center = [], [], [], '', 0
    ords = []
    off2idx = {}
    for tag, body in chunks(path):
        if tag == b'VERT':
            n = struct.unpack('<i', body[:4])[0]
            for i in range(n):
                x, y, z, vn = struct.unpack('<iiii', body[4 + i * 16:4 + i * 16 + 16])
                verts.append((x, y, z, vn & 0xFFFF))
        elif tag == b'NORM':
            n = struct.unpack('<h', body[:2])[0]
            for i in range(n):
                x, y, z, _ = struct.unpack('<hhhh', body[4 + i * 8:4 + i * 8 + 8])
                normals.append((x, y, z))
        elif tag == b'POLY':
            p = 2
            while p + 6 <= len(body):
                n = body[p]
                if n == 0:
                    break
                flags = body[p + 1]
                color = body[p + 2]
                nrm = struct.unpack('<H', body[p + 4:p + 6])[0]
                vs = struct.unpack('<%dH' % n, body[p + 6:p + 6 + 2 * n])
                polys.append((flags, color, nrm, vs))
                p += 6 + 2 * n
        elif tag == b'ORD0':
            center = struct.unpack('<H', body[2:4])[0]
        elif tag[:3] == b'ORD':
            pass
        elif tag == b'NAME':
            name = body.split(b'\0')[0].decode('latin1').strip('"')
    # ポリゴンのバイトオフセット -> インデックス
    p2 = 2
    for i in range(len(polys)):
        off2idx[p2] = i
        p2 += 6 + 2 * len(polys[i][3])

    for tag, body in chunks(path):
        if tag[:3] != b'ORD' or tag == b'ORD0':
            continue
        words = struct.unpack('<%dH' % (len(body) // 2), body)
        sortv = words[1]
        idxs = [off2idx[w] for w in words[2:] if w in off2idx]
        if idxs:
            ords.append((sortv, idxs))

    return name, verts, normals, polys, center, ords


def main():
    os.makedirs(DST, exist_ok=True)

    mat = open(os.path.join(SRC, 'U2E.00M'), 'rb').read()
    pal = bytearray(mat[16:16 + 768])
    pal8 = bytearray((v * 255) // 63 for v in pal)
    open(os.path.join(DST, 'u2e_pal.bin'), 'wb').write(bytes(pal8))
    print('u2e_pal.bin 768')

    tail = struct.unpack('<%dH' % ((len(mat) - 784) // 2), mat[784:])
    count = tail[0]
    inst = list(tail[1:count])   # U2E.C: for(c=1;c<d;c++) reads d-1 entries
    print('instances %d: %s' % (count, inst))

    # ユニークなオブジェクト番号 (0 はカメラなので除外)
    uniq = []
    for v in inst:
        if v and v not in uniq:
            uniq.append(v)
    uniq.sort()
    print('unique objects %d: %s' % (len(uniq), uniq))

    obj_index = {v: i for i, v in enumerate(uniq)}

    out = bytearray()
    out += struct.pack('>H', len(uniq))
    for v in uniq:
        name, verts, normals, polys, center, ords = load_object(
            os.path.join(SRC, 'U2E.%03d' % v))
        kind = 0
        if len(name) > 0 and name[0] == '_':
            kind |= 1
        if name[:3] == 's01':
            kind |= 2
        out += struct.pack('>HHHHH', len(verts), len(normals), len(polys), center, kind)
        for x, y, z, vn in verts:
            out += struct.pack('>iiii', x, y, z, vn)
        for x, y, z in normals:
            out += struct.pack('>hhh', x, y, z)
        for flags, color, nrm, vs in polys:
            out += struct.pack('>BBHH', len(vs), flags, color, nrm)
            out += b''.join(struct.pack('>H', x) for x in vs)
        # ORD リスト (ORD0 以外 = 視線方向ごとの描画順)
        out += struct.pack('>H', len(ords))
        for sortv, idxs in ords:
            out += struct.pack('>HH', sortv, len(idxs))
            out += b''.join(struct.pack('>H', i) for i in idxs)
        print('  U2E.%03d %-12s v=%d n=%d poly=%d ord=%d'
              % (v, name, len(verts), len(normals), len(polys), len(ords)))
    open(os.path.join(DST, 'u2e_obj.bin'), 'wb').write(bytes(out))
    print('u2e_obj.bin', len(out))

    ic = struct.pack('>H', len(inst)) + b''.join(
        struct.pack('>H', 0xFFFF if v == 0 else obj_index[v]) for v in inst)
    open(os.path.join(DST, 'u2e_inst.bin'), 'wb').write(ic)
    print('u2e_inst.bin', len(ic))

    script = open(os.path.join(SRC, 'U2E.0AB'), 'rb').read()
    open(os.path.join(DST, 'u2e_script.bin'), 'wb').write(script)
    print('u2e_script.bin', len(script))


if __name__ == '__main__':
    main()
