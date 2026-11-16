#!/usr/bin/env python3
"""U2A (Vector Part I - space battle) の資産を Amiga 用バイナリへ変換する。

入力 (reference/SecondReality/):
    MAIN/DATA/U2A.00M   パレット + オブジェクトリスト
    MAIN/DATA/U2A.001.. オブジェクト (VERS/NAME/VERT/NORM/POLY/ORD/END)
    MAIN/DATA/U2A.0AB   アニメーションストリーム
    VISU/C/U2ABG.UH     背景 (Uh 形式: 16B + 768B pal + 320x200)

出力 (assets/u2a/):
    u2a_pal.bin    768   パレット (6bit RGB)
    u2a_bg.bin     64000 背景 320x200 chunky
    u2a_obj.bin          オブジェクト (ビッグエンディアン)
        u16 objcount
        for each: u16 vnum, u16 tnum
                  vnum * (s32 x, s32 y, s32 z)
                  tnum * (u16 a, u16 b, u16 c, u8 color, u8 flags)
    u2a_anim.bin   0AB の生ストリーム
    u2a_scene.bin  u16 count + count* u16 object index
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SRC = os.path.join(PROJ, 'reference', 'SecondReality')
DST = os.path.join(PROJ, 'assets', 'u2a')


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


def load_object(path):
    verts = []
    polys = []
    normals = []
    name = ''
    center = 0
    for tag, body in chunks(path):
        if tag == b'VERT':
            n = struct.unpack('<i', body[:4])[0]
            for i in range(n):
                x, y, z, _ = struct.unpack('<iiii', body[4 + i * 16:4 + i * 16 + 16])
                verts.append((x, y, z))
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
            # word0 = リスト長, word1 = 中心頂点 (最寄り頂点)
            center = struct.unpack('<H', body[2:4])[0]
        elif tag == b'NAME':
            name = body.split(b'\0')[0].decode('latin1').strip('"')
    return name, verts, normals, polys, center


def main():
    os.makedirs(DST, exist_ok=True)
    mat = open(os.path.join(SRC, 'MAIN', 'DATA', 'U2A.00M'), 'rb').read()
    pal = bytearray(mat[16:16 + 768])

    bgf = open(os.path.join(SRC, 'VISU', 'C', 'U2ABG.UH'), 'rb').read()
    bg = bgf[16 + 768:16 + 768 + 64000]
    bgpal = bgf[16:16 + 768]
    # U2A.C: memcpy(scene0+16+192*3, bg+16, 64*3)
    pal[192 * 3:256 * 3] = bgpal[192 * 3:256 * 3]

    # 6bit (0-63) -> 8bit (0-255)
    pal8 = bytearray((v * 255) // 63 for v in pal)
    open(os.path.join(DST, 'u2a_pal.bin'), 'wb').write(bytes(pal8))
    open(os.path.join(DST, 'u2a_bg.bin'), 'wb').write(bg)

    objs = []
    for i in (1, 2, 3):
        name, verts, normals, polys, center = load_object(
            os.path.join(SRC, 'MAIN', 'DATA', 'U2A.%03d' % i))
        tris = []
        for flags, color, nrm, vs in polys:
            for k in range(1, len(vs) - 1):
                tris.append((vs[0], vs[k], vs[k + 1], color, flags, nrm))
        objs.append((name, verts, normals, tris, center))
        print('U2A.%03d %-10s verts=%d norms=%d polys=%d tris=%d center=%d'
              % (i, name, len(verts), len(normals), len(polys), len(tris), center))

    out = bytearray()
    out += struct.pack('>H', len(objs))
    for name, verts, normals, tris, center in objs:
        out += struct.pack('>HHHH', len(verts), len(normals), len(tris), center)
        for x, y, z in verts:
            out += struct.pack('>iii', x, y, z)
        for x, y, z in normals:
            out += struct.pack('>hhh', x, y, z)
        for a, b, c, color, flags, nrm in tris:
            out += struct.pack('>HHHHBB', a, b, c, nrm, color, flags)
    open(os.path.join(DST, 'u2a_obj.bin'), 'wb').write(bytes(out))
    print('u2a_obj.bin', len(out), 'bytes')

    anim = open(os.path.join(SRC, 'MAIN', 'DATA', 'U2A.0AB'), 'rb').read()
    open(os.path.join(DST, 'u2a_anim.bin'), 'wb').write(anim)
    print('u2a_anim.bin', len(anim))

    # オブジェクトリスト: .00M の offset 784 以降 (16bit)
    tail = struct.unpack('<%dH' % ((len(mat) - 784) // 2), mat[784:])
    count = tail[0]
    idx = list(tail[1:count])
    print('object list count=%d idx=%s' % (count, idx))
    sc = struct.pack('>H', len(idx)) + b''.join(struct.pack('>H', v) for v in idx)
    open(os.path.join(DST, 'u2a_scene.bin'), 'wb').write(sc)

    # アニメーションのデコード検証
    decode_test(anim)


def decode_test(anim):
    sp = 0
    frames = 0
    obj_used = set()
    recs = 0
    maxobj = 0
    fovs = {}
    while sp < len(anim) and frames < 5000:
        a = anim[sp]; sp += 1
        if a == 0xff:
            a = anim[sp]; sp += 1
            if a <= 0x7f:
                frames += 1
                fovs[a] = fovs.get(a, 0) + 1
                continue
            elif a == 0xff:
                print('anim end at frame', frames)
                break
        if (a & 0xc0) == 0xc0:
            onum = (a & 0x3f) << 4
            a = anim[sp]; sp += 1
        else:
            onum = 0
        onum = (onum & 0xff0) | (a & 0xf)
        obj_used.add(onum)
        maxobj = max(maxobj, onum)
        pflag = 0
        sel = (a & 0x30) >> 4
        if sel == 1:
            pflag = anim[sp]; sp += 1
        elif sel == 2:
            pflag = anim[sp] | (anim[sp + 1] << 8); sp += 2
        elif sel == 3:
            pflag = anim[sp] | (anim[sp + 1] << 8) | (anim[sp + 2] << 16); sp += 3

        def lsget(f):
            nonlocal sp
            n = f & 3
            if n == 0:
                return 0
            v = 0
            for i in range(n):
                b = anim[sp]; sp += 1
                if i == n - 1 and b >= 0x80:
                    v |= (b - 256) << (8 * i)
                else:
                    v |= b << (8 * i)
            return v

        lsget(pflag)
        lsget(pflag >> 2)
        lsget(pflag >> 4)
        if pflag & 0x40:
            for b in range(9):
                if pflag & (0x80 << b):
                    lsget(2)
        else:
            for b in range(9):
                if pflag & (0x80 << b):
                    lsget(1)
        recs += 1
        if sp > len(anim):
            print('anim overrun at', sp)
            break
    print('anim: frames=%d records=%d objects=%s maxobj=%d fovs=%s'
          % (frames, recs, sorted(obj_used), maxobj, dict(sorted(fovs.items())[:10])))


if __name__ == '__main__':
    main()
