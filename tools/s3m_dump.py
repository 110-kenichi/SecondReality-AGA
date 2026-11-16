"""S3M detailed analyzer: orders, instruments, effect command histogram,
and dump of cells containing unusual commands (candidate STMIK zinfo markers).

Usage: python s3m_dump.py <file.s3m> [--dump-orders] [--dump-cmds]
"""
import struct
import sys
from collections import Counter, defaultdict

STD_CMDS = {
    1: 'A', 2: 'B', 3: 'C', 4: 'D', 5: 'E', 6: 'F', 7: 'G', 8: 'H',
    9: 'I', 10: 'J', 11: 'K', 12: 'L', 13: 'M', 14: 'N', 15: 'O', 16: 'P',
    17: 'Q', 18: 'R', 19: 'S', 20: 'T', 21: 'U', 22: 'V', 23: 'W', 24: 'X',
    25: 'Y', 26: 'Z',
}

def parse(path):
    d = open(path, 'rb').read()
    assert d[0x2C:0x30] == b'SCRM', 'not an S3M'
    name = d[0x00:0x1C].split(b'\0')[0].decode('cp437', 'replace').strip()
    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    flags, cwtv, ffi = struct.unpack_from('<HHH', d, 0x26)
    gvol, speed, tempo, mvol, ultra, defpan = struct.unpack_from('<6B', d, 0x30)
    chan_settings = d[0x40:0x60]
    enabled = [i for i, c in enumerate(chan_settings) if 0 < c < 0x80]
    mastervol = d[0x33]
    orders = list(d[0x60:0x60 + ordnum])
    ins_pp = list(struct.unpack_from('<%dH' % insnum, d, 0x60 + ordnum))
    pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))

    print(f"== {path}")
    print(f"  name='{name}' ordnum={ordnum} insnum={insnum} patnum={patnum}")
    print(f"  flags=0x{flags:04X} cwtv=0x{cwtv:04X} ffi=0x{ffi:04X}")
    print(f"  gvol={gvol} speed={speed} tempo={tempo} mvol={mvol} ultraclick={ultra} defpan={defpan}")
    print(f"  mastervol={mastervol} channels_enabled={enabled}")

    # Order list
    real_orders = [o for o in orders if o < 254]
    print(f"  orders ({len(orders)}):")
    line = []
    for i, o in enumerate(orders):
        tag = 'SKIP' if o == 254 else ('END' if o == 255 else str(o))
        line.append(f"{i}:{tag}")
    print("    " + " ".join(line))

    # Instruments
    print("  instruments:")
    ins_list = []
    for i, pp in enumerate(ins_pp):
        off = pp * 16
        if off == 0:
            continue
        typ = d[off]
        fname = d[off + 1:off + 13].split(b'\0')[0].decode('cp437', 'replace')
        # 標準 S3M 楽器レイアウト: len@17, loop@21/25, vol@29, pack@31, flags@32, c2spd@33
        (length, loopbeg, loopend) = struct.unpack_from('<LLL', d, off + 17)
        vol = d[off + 29]
        pack = d[off + 31]
        iflags = d[off + 32]
        c2spd = struct.unpack_from('<H', d, off + 33)[0]
        name2 = d[off + 48:off + 76].split(b'\0')[0].decode('cp437', 'replace')
        ptr = struct.unpack_from('<H', d, off + 13)[0] | (d[off + 15] << 16)
        ins_list.append((i, typ, name2 or fname, length, loopbeg, loopend, vol, pack, iflags, c2spd, ptr))
        print(f"    #{i:2d} type={typ} '{name2 or fname}' len={length} loop={loopbeg}-{loopend} "
              f"vol={vol} pack={pack} flags=0x{iflags:02X} c2spd={c2spd} data@{ptr*16 if ptr else 0}")

    # Patterns
    cmd_hist = Counter()
    unusual = []  # (pat, row, ch, note, ins, vol, cmd, info)
    note_hist = Counter()
    for p in range(patnum):
        off = pat_pp[p] * 16
        if off == 0:
            continue
        ptr = off + 2
        row = 0
        while row < 64:
            b = d[ptr]; ptr += 1
            if b == 0:
                row += 1
                continue
            ch = b & 0x1F
            note = ins = vol = cmd = info = None
            if b & 0x20:
                note = d[ptr]; ins = d[ptr + 1]; ptr += 2
                if note not in (255, 254):
                    note_hist[note] += 1
            if b & 0x40:
                vol = d[ptr]; ptr += 1
            if b & 0x80:
                cmd = d[ptr]; info = d[ptr + 1]; ptr += 2
                cmd_hist[cmd] += 1
                if cmd == 0 or cmd > 26:
                    unusual.append((p, row, ch, note, ins, vol, cmd, info))
                elif cmd == 26:  # 'Z' - non-standard, candidate zinfo
                    unusual.append((p, row, ch, note, ins, vol, cmd, info))
                elif cmd == 19:  # 'S' special
                    pass
    print("  effect command histogram:")
    for c, n in sorted(cmd_hist.items()):
        nm = STD_CMDS.get(c, '???')
        print(f"    cmd {c:2d} ('{nm}'): {n}")
    print(f"  unusual cells (cmd 0, cmd>26, or 'Z'=26): {len(unusual)}")
    for (p, row, ch, note, ins, vol, cmd, info) in unusual[:60]:
        print(f"    pat={p:2d} row={row:2d} ch={ch} note={note} ins={ins} vol={vol} cmd={cmd} info=0x{info:02X}")

    # 'S' special sub-command histogram
    s_hist = Counter()
    for p in range(patnum):
        off = pat_pp[p] * 16
        if off == 0:
            continue
        ptr = off + 2
        row = 0
        while row < 64:
            b = d[ptr]; ptr += 1
            if b == 0:
                row += 1
                continue
            if b & 0x20:
                ptr += 2
            if b & 0x40:
                ptr += 1
            if b & 0x80:
                cmd = d[ptr]; info = d[ptr + 1]; ptr += 2
                if cmd == 19:
                    s_hist[info >> 4] += 1
    print(f"  'S' subcommands: {dict(sorted(s_hist.items()))}")

for f in sys.argv[1:]:
    parse(f)
