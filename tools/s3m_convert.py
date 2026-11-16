"""Convert a standard S3M module to the Second Reality A1200 Amiga module format.

Output format (little-endian):
  struct ModuleHeader {
      u32 magic;              // 'SRM1'
      u16 num_orders;
      u16 num_instruments;
      u16 num_patterns;
      u8  num_channels;       // 8
      u8  init_speed;         // from header
      u8  init_tempo;
      u8  mastervol;
      u32 order_list_offset;      // u16[num_orders]
      u32 instruments_offset;     // Instrument[num_instruments]
      u32 patterns_offset;        // u32[num_patterns] offsets (absolute from module start)
      u32 samples_offset;         // sample data base
  }
  Instrument {
      char name[28];
      u8  volume;
      u8  flags;              // bit0 loop
      u16 c2spd;
      u32 length;
      u32 loop_start;
      u32 loop_end;
      u32 data_offset;        // absolute offset from module start
  }
  Pattern: 64 rows * 8 channels * 5 bytes: note, ins, vol, cmd, info
      note: 255 = none; S3M note values preserved (1..96); 254 = note cut
      vol: 255 = none
      cmd: 0 = none (Z command mapped to cmd 26)
  Sample data: signed 8-bit PCM

Usage: python s3m_convert.py <in.s3m> <out.bin> [--hexdump]
"""
import struct
import sys

MAGIC = 0x53524D31  # 'SRM1' (ビッグエンディアンで書くと 53 52 4D 31)


def parse_instrument(d, off):
    typ = d[off]
    if typ != 1:
        return None
    length, loopbeg, loopend = struct.unpack_from('<LLL', d, off + 16)
    vol = d[off + 28]
    pack = d[off + 30]
    flags = d[off + 31]
    c2spd = struct.unpack_from('<H', d, off + 32)[0]
    name = d[off + 48:off + 76].split(b'\0')[0].decode('cp437', 'replace')
    ptr_para = (d[off + 13] << 16) | struct.unpack_from('<H', d, off + 14)[0]
    return {
        'length': length, 'loopbeg': loopbeg, 'loopend': loopend,
        'vol': vol, 'pack': pack, 'flags': flags, 'c2spd': c2spd,
        'name': name, 'ptr': ptr_para * 16,
    }


def decode_dp30(data):
    """Decode S3M DP30 ADPCM (pack=1): 4-bit signed deltas, 2 samples/byte."""
    out = bytearray()
    for b in data:
        hi = b >> 4
        lo = b & 0xF
        for nib in (hi, lo):
            if nib & 8:
                nib -= 16
            out.append(nib & 0xFF)
    return bytes(out)


def get_sample(d, ins):
    """Return raw sample bytes (8-bit signed) for an instrument."""
    raw = d[ins['ptr']:ins['ptr'] + ins['length']]
    if ins['pack'] == 1:
        # DP30 ADPCM: decode deltas then accumulate
        deltas = decode_dp30(raw)
        out = bytearray(len(deltas))
        acc = 0
        for i, dv in enumerate(deltas):
            acc = (acc + dv) & 0xFF
            out[i] = acc
        raw = bytes(out)
    # 16-bit samples: flags bit 2
    if ins['flags'] & 4:
        # take the high byte of each 16-bit little-endian sample
        out = bytearray()
        for i in range(0, len(raw) - 1, 2):
            out.append(raw[i + 1])
        raw = bytes(out)
        ins['length'] //= 2
        ins['loopbeg'] //= 2
        ins['loopend'] //= 2
    # convert unsigned PCM -> signed
    out = bytearray(raw)
    for i in range(len(out)):
        out[i] ^= 0x80
    return bytes(out)


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    d = open(src, 'rb').read()
    assert d[0x2C:0x30] == b'SCRM'

    ordnum, insnum, patnum = struct.unpack_from('<HHH', d, 0x20)
    gvol, speed, tempo, mvol = d[0x30], d[0x31], d[0x32], d[0x33]
    orders = list(d[0x60:0x60 + ordnum])          # オーダーリストはバイト列
    ins_pp = list(struct.unpack_from('<%dH' % insnum, d, 0x60 + ordnum))
    pat_pp = list(struct.unpack_from('<%dH' % patnum, d, 0x60 + ordnum + 2 * insnum))

    # Build output (Amiga = ビッグエンディアン、4 バイトアライン)
    out = bytearray()
    hdr_size = 4 + 2 * 3 + 4 + 2 + 4 * 4          # 32 バイト
    order_off = hdr_size
    instruments_off = (order_off + ordnum + 3) & ~3    # オーダーリストはバイト列
    inst_size = 48
    patterns_tbl_off = (instruments_off + insnum * inst_size + 3) & ~3
    patterns_off = (patterns_tbl_off + patnum * 4 + 3) & ~3

    # Patterns first (to know offsets), then instruments+samples
    pat_blobs = []
    for p in range(patnum):
        off = pat_pp[p] * 16
        cells = bytearray()
        if off == 0:
            cells = bytearray(bytes([0xFF, 0, 0xFF, 0, 0]) * (64 * 8))
        else:
            ptr = off + 2
            row = 0
            rowcells = [[0xFF, 0, 0xFF, 0, 0] for _ in range(8)]  # note, ins, vol, cmd, info
            while row < 64:
                b = d[ptr]; ptr += 1
                if b == 0:
                    for c in rowcells:
                        cells += bytes(c)
                    rowcells = [[0xFF, 0, 0xFF, 0, 0] for _ in range(8)]
                    row += 1
                    continue
                ch = b & 0x1F
                if ch >= 8:
                    # disabled channel: consume bytes but ignore the cell
                    # (ch8..31 の A/T はオリジナルでも実行されない)
                    if b & 0x20:
                        ptr += 2
                    if b & 0x40:
                        ptr += 1
                    if b & 0x80:
                        ptr += 2
                    continue
                c = rowcells[ch]
                if b & 0x20:
                    c[0] = d[ptr]; c[1] = d[ptr + 1]; ptr += 2
                if b & 0x40:
                    c[2] = d[ptr]; ptr += 1
                if b & 0x80:
                    c[3] = d[ptr]; c[4] = d[ptr + 1]; ptr += 2
        pat_blobs.append(bytes(cells))

    # sample data area after patterns (4 バイトアライン)
    data_cursor = (patterns_off + sum(len(p) for p in pat_blobs) + 3) & ~3
    samples_off = data_cursor

    inst_blobs = []
    sample_blobs = []
    cursor = samples_off
    for i in range(insnum):
        off = ins_pp[i] * 16
        ins = parse_instrument(d, off) if off else None
        if ins is None:
            inst_blobs.append(struct.pack('>28sBBHLLLL', b'', 0, 0, 0, 0, 0, 0, 0))
            continue
        samp = get_sample(d, ins)
        inst_blobs.append(struct.pack(
            '>28sBBHLLLL', ins['name'].encode('cp437', 'replace')[:28], ins['vol'],
            ins['flags'] & 1, ins['c2spd'], len(samp), ins['loopbeg'], ins['loopend'], cursor))
        sample_blobs.append(samp)
        cursor += len(samp)

    # header (Amiga = ビッグエンディアン)
    out += struct.pack('>IHHHBBBBH', MAGIC, ordnum, insnum, patnum, 8, speed, tempo, mvol, 0)
    out += struct.pack('>IIII', order_off, instruments_off, patterns_tbl_off, samples_off)
    assert len(out) == hdr_size, (len(out), hdr_size)
    out += bytes(orders)
    out += b'\0' * (instruments_off - len(out))
    assert len(out) == instruments_off
    for b in inst_blobs:
        out += b
    out += b'\0' * (patterns_tbl_off - len(out))
    assert len(out) == patterns_tbl_off
    pat_offs = []
    c = patterns_off
    for p in pat_blobs:
        pat_offs.append(c)
        c += len(p)
    out += struct.pack('>%dI' % patnum, *pat_offs)
    assert len(out) == patterns_off
    for p in pat_blobs:
        out += p
    out += b'\0' * (samples_off - len(out))
    assert len(out) == samples_off, (len(out), samples_off)
    for s in sample_blobs:
        out += s

    open(dst, 'wb').write(out)
    print(f"wrote {dst}: {len(out)} bytes")
    print(f"  orders={ordnum} instruments={insnum} patterns={patnum}")
    print(f"  patterns {patterns_off}..{samples_off} ({samples_off - patterns_off} bytes)")
    print(f"  samples {samples_off}..{len(out)} ({len(out) - samples_off} bytes)")
    # summary of instruments
    for i, b in enumerate(inst_blobs):
        (name, vol, flags, c2spd, length, lb, le, doff) = struct.unpack('>28sBBHLLLL', b)
        name_s = name.split(b'\0')[0].decode('cp437', 'replace')
        print(f"  ins {i:2d}: '{name_s}' len={length} loop={lb}-{le} vol={vol} flags={flags} c2spd={c2spd}")


if __name__ == '__main__':
    main()
