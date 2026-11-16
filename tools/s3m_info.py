"""Dump S3M header info and count real channel usage (libxmp-compatible unpacking)."""
import struct
import sys
import glob

def parse(path):
    d = open(path, 'rb').read()
    if d[0x2C:0x30] != b'SCRM':
        print(f"{path}: not an S3M")
        return
    name = d[0x00:0x1C].split(b'\0')[0].decode('cp437', 'replace').strip()
    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    flags, cwtv, ffi = struct.unpack_from('<HHH', d, 0x26)
    gvol, speed, tempo, mvol, ultra, defpan = struct.unpack_from('<6B', d, 0x30)
    chan_settings = d[0x40:0x60]
    enabled = [i for i, c in enumerate(chan_settings) if 0 < c < 0x80]
    orders = d[0x60:0x60 + ordnum]
    ins_pp = struct.unpack_from('<%dH' % insnum, d, 0x60 + ordnum)
    pat_pp = struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum)

    notes_by_chan = {}
    cell_by_chan = {}
    rows_total = 0
    patterns_with_notes = 0
    for p in range(patnum):
        off = pat_pp[p] * 16
        if off == 0 or off + 2 > len(d):
            continue
        pat_len = struct.unpack_from('<H', d, off)[0] - 2
        ptr = off + 2
        row = 0
        has_note = False
        while pat_len >= 0 and row < 64 and ptr < len(d):
            b = d[ptr]; ptr += 1
            if b == 0:
                row += 1
                continue
            ch = b & 0x1F
            cell_by_chan[ch] = cell_by_chan.get(ch, 0) + 1
            if b & 0x20:
                note = d[ptr]; ins = d[ptr + 1]; ptr += 2
                pat_len -= 2
                if note not in (255, 254):
                    notes_by_chan[ch] = notes_by_chan.get(ch, 0) + 1
                    has_note = True
            if b & 0x40:
                ptr += 1; pat_len -= 1
            if b & 0x80:
                ptr += 2; pat_len -= 2
        rows_total += row
        if has_note:
            patterns_with_notes += 1

    print(f"== {path}")
    print(f"  name        : {name}")
    print(f"  orders={ordnum} instruments={insnum} patterns={patnum} (patterns w/ notes: {patterns_with_notes})")
    print(f"  cwtv=0x{cwtv:04X} init_speed={speed} init_tempo={tempo}")
    print(f"  channels enabled (header): {enabled}")
    print(f"  channels with note cells : {sorted(notes_by_chan)}")
    print(f"  note count per channel   : {dict(sorted(notes_by_chan.items()))}")
    print(f"  total note cells         : {sum(notes_by_chan.values())}")
    print()

for f in sys.argv[1:] or glob.glob('**/*.S3M', recursive=True):
    parse(f)
