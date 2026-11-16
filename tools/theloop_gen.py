"""Translate COMAN/THELOOP.INC (386 unrolled voxel column renderer) into C.

The original is 5,207 lines of straight-line assembly (192 unrolled steps with
642 labels).  Hand-porting that is error prone, so every instruction is mapped
1:1 onto C with the same register widths and carry behaviour.  The generated
code is therefore bit-exact with the original.

Usage:  python tools/theloop_gen.py
Output: src/m2/parts/theloop_gen.c
"""
import re
from pathlib import Path

SRC = Path(r"reference/SecondReality/COMAN/THELOOP.INC")
OUT = Path(r"src/m2/parts/theloop_gen.c")

# Register model
#   eax -> ea (u32), ax = low 16 bits
#   ecx -> ec (u32), cx = low 16 bits
#   bx / dx / si / di / bp -> u16
#   fs:[si] -> tl_wave1[si/2], gs:[di] -> tl_wave2[di/2]
#   es:[bp] -> tl_vbuf[bp]  (64K frame buffer, 16-bit offsets wrap)


def imm(tok):
    tok = tok.strip()
    tok = re.sub(r'([0-9][0-9A-Fa-f]*)h', r'0x\1', tok)
    return int(eval(tok, {"__builtins__": {}}, {}))


def es_index(arg):
    m = re.match(r'es:\[bp(?:-(\d+))?\]$', arg)
    if not m:
        raise SystemExit('bad es operand: %r' % arg)
    off = int(m.group(1)) if m.group(1) else 0
    return '(u16)(bp - %d)' % off if off else 'bp'


def label(name):
    return 'L_' + re.sub(r'[^A-Za-z0-9_]', '', name.replace('_@', ''))


def translate():
    src = SRC.read_text(encoding='latin-1').splitlines()
    body = []
    started = False

    for raw in src:
        line = raw.split(';')[0].strip()
        if not line:
            continue
        if not started:
            if line.lower().startswith('_@seek0:'):
                started = True
            else:
                continue

        low = line.lower()
        if low == 'theloop endp':
            break
        if line.endswith(':'):
            body.append('%s: ;' % label(line[:-1]))
            continue
        if low == 'ret':
            body.append('    return;')
            continue

        m = re.match(r'^(add|adc|sub|cmp|mov|lea|shr|jge|jmp)\s+(.*)$', low)
        if not m:
            raise SystemExit('unhandled: %r' % line)
        op, a = m.group(1), m.group(2)

        if op == 'mov':
            d, s = [x.strip() for x in a.split(',', 1)]
            if d == 'bx' and s.startswith('fs:'):
                body.append('    bx = tl_wave1[(si >> 1) & 0x7FFFu];')
            elif d == 'bx' and s.startswith('gs:'):
                body.append('    bx = (u16)(bx + tl_wave2[(di >> 1) & 0x7FFFu]);')
            elif d.startswith('es:'):
                body.append('    tl_vbuf[%s] = (u8)dx;' % es_index(d))
            else:
                raise SystemExit('unhandled mov: %r' % line)

        elif op == 'add':
            d, s = [x.strip() for x in a.split(',', 1)]
            if d == 'eax' and s == 'ecx':
                body.append('    { u32 t = ea + ec; cf = (t < ea); ea = t; }')
            elif d == 'eax':
                body.append('    { u32 t = ea + 0x%Xu; cf = (t < ea); ea = t; }' % imm(s))
            elif d == 'ecx':
                body.append('    { u32 t = ec + 0x%Xu; cf = (t < ec); ec = t; }' % imm(s))
            elif d == 'si':
                body.append('    si = (u16)(si + xsina%s);' % ('1' if s.endswith('1') else '2'))
            elif d == 'di':
                body.append('    di = (u16)(di + ysina%s);' % ('1' if s.endswith('1') else '2'))
            elif d == 'bx' and s.startswith('gs:'):
                body.append('    bx = (u16)(bx + tl_wave2[(di >> 1) & 0x7FFFu]);')
            elif d == 'bx':
                body.append('    bx = (u16)(bx + (u16)0x%X);' % (imm(s) & 0xFFFF))
            else:
                raise SystemExit('unhandled add: %r' % line)

        elif op == 'adc':
            d, s = [x.strip() for x in a.split(',', 1)]
            if d == 'eax' and s == 'ecx':
                body.append('    { u32 t = ea + ec; u32 c = (t < ea);'
                            ' u32 u = t + (u32)cf; c |= (u < t); ea = u; cf = (int)c; }')
            elif d == 'ax':
                body.append('    { u32 q = (u32)(u16)ea + (u32)(u16)0x%X + (u32)cf;'
                            ' cf = (int)(q >> 16); ea = (ea & 0xFFFF0000u) | (q & 0xFFFFu); }'
                            % (imm(s) & 0xFFFF))
            elif d == 'cx':
                body.append('    { u32 q = (u32)(u16)ec + (u32)(u16)0x%X + (u32)cf;'
                            ' cf = (int)(q >> 16); ec = (ec & 0xFFFF0000u) | (q & 0xFFFFu); }'
                            % (imm(s) & 0xFFFF))
            elif d == 'ecx':
                body.append('    { u32 t = ec + 0x%Xu; u32 c = (t < ec);'
                            ' u32 u = t + (u32)cf; c |= (u < t); ec = u; cf = (int)c; }' % imm(s))
            else:
                raise SystemExit('unhandled adc: %r' % line)

        elif op == 'sub':
            d, s = [x.strip() for x in a.split(',', 1)]
            if d != 'bp':
                raise SystemExit('unhandled sub: %r' % line)
            body.append('    bp = (u16)(bp - %d);' % (imm(s) & 0xFFFF))

        elif op == 'cmp':
            d, s = [x.strip() for x in a.split(',', 1)]
            if d != 'ax' or s != 'bx':
                raise SystemExit('unhandled cmp: %r' % line)
            # cmp sets BOTH the unsigned borrow (CF) and the signed flags.
            # CF is what the following adc instructions consume.
            body.append('    cf = ((u16)ea < (u16)bx);')

        elif op == 'lea':
            d, s = [x.strip() for x in a.split(',', 1)]
            mm = re.match(r'\[bx\+(-?\d+)\]$', s)
            if d != 'dx' or not mm:
                raise SystemExit('unhandled lea: %r' % line)
            body.append('    dx = (u16)(bx + %d);' % int(mm.group(1)))

        elif op == 'shr':
            d, s = [x.strip() for x in a.split(',', 1)]
            if d != 'dl':
                raise SystemExit('unhandled shr: %r' % line)
            body.append('    dx = (u16)((dx & 0xFF00u) | ((dx & 0xFFu) >> %d));' % int(s))

        elif op == 'jge':
            # jge tests SF==OF, i.e. the signed comparison, not CF
            body.append('    if ((s16)(u16)ea >= (s16)bx) goto %s;'
                        % label(a.replace('short', '').strip()))

        elif op == 'jmp':
            body.append('    goto %s;' % label(a.strip()))

    return body


HEADER = '''/*
 * theloop_gen.c -- GENERATED by tools/theloop_gen.py from
 * reference/SecondReality/COMAN/THELOOP.INC.  Do not edit by hand.
 *
 * 1:1 translation of the original 386 unrolled voxel column renderer
 * (192 steps).  Register widths and the carry chain are preserved, so the
 * result is bit-exact with the original part.
 */
#include "theloop_gen.h"

void theloop_run(void)
{
    u32 ea = 0, ec = 0xEC00FFFAu;
    u16 bx = 0, dx = 0;
    u16 si = tl_si, di = tl_di, bp = tl_bp;
    u16 xsina1 = tl_cx;
    u16 ysina1 = tl_dy;
    u16 xsina2 = (u16)(tl_cx + tl_cx);
    u16 ysina2 = (u16)(tl_dy + tl_dy);
    int cf = 0;

'''


def main():
    body = translate()
    OUT.write_text(HEADER + '\n'.join(body) + '\n}\n', encoding='utf-8', newline='\n')
    print('wrote %s (%d statements)' % (OUT, len(body)))


if __name__ == '__main__':
    main()
