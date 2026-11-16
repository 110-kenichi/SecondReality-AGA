"""Parse an OMF object file (Intel), extract segments and fixups, and
disassemble code around references to a given external symbol.

Usage: python omf_stmik.py <file.300> <symbol> [--list-symbols]
"""
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
except ImportError:
    Cs = None


class OMF:
    def __init__(self, data):
        self.d = data
        self.pos = 0
        self.lnames = []
        self.segdefs = []      # list of dicts
        self.extdefs = []      # list of names (index 1-based)
        self.pubdefs = []      # (seg, offset, name)
        self.fixups = []       # dicts
        self.threads = []      # 4 frame + 4 target threads (by index)
        self.segments = {}     # seg_index (1-based) -> bytearray
        self.ledata_last = None

    def u16(self, b, o):
        return struct.unpack_from('<H', b, o)[0]

    def idx(self, b, o):
        """Parse an index field (1 or 2 bytes). Returns (value, new_offset)."""
        if b[o] & 0x80:
            return ((b[o] & 0x7F) << 8) | b[o + 1], o + 2
        return b[o], o + 1

    def parse(self):
        d = self.d
        while self.pos < len(d):
            rectype = d[self.pos]
            length = self.u16(d, self.pos + 1)
            # length includes the checksum byte
            payload = d[self.pos + 3:self.pos + 3 + length - 1]
            self.pos += 3 + length
            try:
                self.handle(rectype, payload)
            except Exception as e:
                print(f"  (record 0x{rectype:02X} parse error: {e})")

    def handle(self, rectype, p):
        if rectype == 0x96:  # LNAMES
            o = 0
            while o < len(p):
                n = p[o]; o += 1
                self.lnames.append(p[o:o + n].decode('ascii', 'replace')); o += n
        elif rectype == 0x98:  # SEGDEF
            acbp = p[0]
            o = 1
            if acbp & 1:  # use32
                size = struct.unpack_from('<I', p, o)[0]; o += 4
            else:
                size = self.u16(p, o); o += 2
            name_idx, o = self.idx(p, o)
            class_idx, o = self.idx(p, o)
            overlay_idx, o = self.idx(p, o)
            name = self.lnames[name_idx - 1] if name_idx else ''
            cls = self.lnames[class_idx - 1] if class_idx else ''
            self.segdefs.append({'size': size, 'name': name, 'class': cls, 'use32': acbp & 1})
        elif rectype == 0x8C:  # EXTDEF
            o = 0
            while o < len(p):
                n = p[o]; o += 1
                name = p[o:o + n].decode('ascii', 'replace'); o += n
                _, o = self.idx(p, o)  # type index
                self.extdefs.append(name)
        elif rectype == 0x90:  # PUBDEF
            grp, o = self.idx(p, 0)
            seg, o = self.idx(p, o)
            if seg == 0:
                _, o = self.idx(p, o)  # base frame
            while o < len(p):
                n = p[o]; o += 1
                name = p[o:o + n].decode('ascii', 'replace'); o += n
                off = self.u16(p, o); o += 2
                _, o = self.idx(p, o)
                self.pubdefs.append((seg, off, name))
        elif rectype == 0x9A or rectype == 0x9C:  # FIXUPP (16/32)
            o = 0
            while o < len(p):
                b0 = p[o]
                if b0 & 0x80:  # FIXUP
                    M = (b0 >> 6) & 1
                    loc = (b0 >> 2) & 0xF
                    dro = ((b0 & 3) << 8) | p[o + 1]
                    fixdat = p[o + 2]
                    o += 3
                    F = (fixdat >> 7) & 1
                    frame = (fixdat >> 4) & 7
                    T = (fixdat >> 3) & 1
                    P = (fixdat >> 2) & 1
                    targt = fixdat & 3
                    frame_datum = None
                    if F == 0 and frame in (0, 1, 2):
                        frame_datum, o = self.idx(p, o)
                    target_datum = None
                    target_method = None
                    if T == 0:
                        target_method = (P << 2) | targt
                        target_datum, o = self.idx(p, o)
                    disp = None
                    if P == 0:
                        if rectype == 0x9C:
                            disp = struct.unpack_from('<I', p, o)[0]; o += 4
                        else:
                            disp = self.u16(p, o); o += 2
                    self.fixups.append({
                        'M': M, 'loc': loc, 'dro': dro, 'F': F, 'frame': frame,
                        'T': T, 'P': P, 'targt': targt,
                        'frame_datum': frame_datum, 'target_datum': target_datum,
                        'target_method': target_method, 'disp': disp,
                        'ledata': self.ledata_last,
                    })
                else:  # THREAD
                    D = (b0 >> 6) & 1
                    method = (b0 >> 2) & 7
                    thred = b0 & 3
                    o += 1
                    index = None
                    if D == 1 and method in (0, 1, 2):
                        index, o = self.idx(p, o)
                    elif D == 0 and method != 3:
                        index, o = self.idx(p, o)
                    while len(self.threads) <= thred:
                        self.threads.append({})
                    self.threads[thred] = {'D': D, 'method': method, 'index': index}
        elif rectype == 0xA0 or rectype == 0xA1:  # LEDATA
            seg, o = self.idx(p, 0)
            if rectype == 0xA1:
                off = struct.unpack_from('<I', p, o)[0]; o += 4
            else:
                off = self.u16(p, o); o += 2
            data = p[o:]
            self.ledata_last = (seg, off, len(data))
            segimg = self.segments.setdefault(seg, bytearray())
            if len(segimg) < off + len(data):
                segimg.extend(b'\0' * (off + len(data) - len(segimg)))
            segimg[off:off + len(data)] = data

    def resolve_target(self, fx):
        """Return ('ext', index) / ('seg', index) / ('grp', index) / None."""
        if fx['T'] == 0:
            method = fx['target_method']
            if method in (2, 6):
                return ('ext', fx['target_datum'])
            if method in (0, 4):
                return ('seg', fx['target_datum'])
            return ('grp', fx['target_datum'])
        # target thread
        th = self.threads[fx['targt']]
        method = th['method']
        P = fx['P']
        full = ((P << 2) | (method & 3)) if method < 4 else method
        if method in (2, 6):
            return ('ext', th['index'])
        if method in (0, 4):
            return ('seg', th['index'])
        return ('grp', th['index'])


def main():
    data = open(sys.argv[1], 'rb').read()
    symbol = sys.argv[2] if len(sys.argv) > 2 else None
    omf = OMF(data)
    omf.parse()

    print(f"segments: {len(omf.segdefs)}")
    for i, s in enumerate(omf.segdefs, 1):
        print(f"  seg {i}: name='{s['name']}' class='{s['class']}' size={s['size']} use32={s['use32']} "
              f"loaded={len(omf.segments.get(i, b''))}")
    print(f"externals: {len(omf.extdefs)}")
    for i, e in enumerate(omf.extdefs, 1):
        print(f"  ext {i}: {e}")
    print(f"publics: {len(omf.pubdefs)}")
    for seg, off, name in omf.pubdefs:
        print(f"  pub seg{seg}:{off:04X} {name}")
    print(f"fixups: {len(omf.fixups)}")

    if symbol:
        sym_off = None
        for seg, off, name in omf.pubdefs:
            if name == symbol:
                sym_off = off
        if sym_off is None:
            print(f"symbol {symbol} not found in publics")
            return
        print(f"\n=== {symbol} @ seg1:0x{sym_off:04X} ===")

        img = omf.segments.get(1, b'')
        needle = struct.pack('<H', sym_off)
        hits = []
        i = 0
        while True:
            j = img.find(needle, i)
            if j < 0:
                break
            hits.append(j)
            i = j + 1
        print(f"{len(hits)} code locations contain the address bytes:")
        for h in hits:
            print(f"  at 0x{h:04X}")

        if Cs:
            md = Cs(CS_ARCH_X86, CS_MODE_16)
            print(f"\n=== disassembly around address occurrences ===")
            shown = set()
            for h in hits:
                start = max(0, h - 12)
                if start in shown:
                    continue
                shown.add(start)
                print(f"\n--- around 0x{h:04X} ---")
                for insn in md.disasm(bytes(img[start:h + 10]), start):
                    mark = " <<<" if insn.address <= h < insn.address + insn.size else ""
                    print(f"  {insn.address:04X}: {insn.mnemonic} {insn.op_str}{mark}")


if __name__ == "__main__":
    main()
