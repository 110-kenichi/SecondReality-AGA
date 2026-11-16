"""Measure the music row rate from the rb*.png debug-bar captures.

Bars (screen coords, 320x256):
  row   : x=80..128,  y=160-h..159, h=np_row>>1
  speed : x=144..192, y=160-h..159, h=np_speed<<2
  sec   : x=208..256, y=160-h..159, h=(vbl/50)&31
  ord   : x=144..192, y=96-h..95,  h=np_ord&31
"""
import csv, sys, glob, os
import numpy as np
from PIL import Image

CSV = 'run/rb_times.csv'

def run_up(col, bottom, thr=170):
    y = bottom
    n = 0
    while y >= 0 and col[y].min() >= thr:
        n += 1
        y -= 1
    return n

def bar_bottom(scr, x, lo, hi, thr=170):
    """find the lowest y in [lo,hi] that is white, scanning from hi."""
    col = scr[:, x]
    for y in range(hi, lo - 1, -1):
        if col[y].min() >= thr:
            return y
    return None

def main():
    rows = []
    with open(CSV) as f:
        for r in csv.DictReader(f):
            rows.append((int(r['idx']), int(r['ms']), r['path']))
    if not rows:
        print('no rows')
        return
    data = []
    for idx, ms, path in rows:
        im = np.asarray(Image.open(path).convert('RGB')).astype(int)
        scr = im[31:599, 65:705]
        # speed bar: x=144 screen -> capture 353
        sb = bar_bottom(scr, 353, 300, 400)
        if sb is None:
            data.append((idx, ms, None, None, None, None))
            continue
        spx = run_up(scr[:, 353], sb)
        data.append((idx, ms, sb, spx, None, None))
    # calibrate B from the speed bar (should be 12 rows for speed 3)
    spxs = [d[3] for d in data if d[3]]
    if not spxs:
        print('no speed bar'); return
    spx = max(set(spxs), key=spxs.count)
    B = spx / 12.0
    print('speed bar px mode', spx, '-> B(px/row)', round(B, 4), 'counts', {v: spxs.count(v) for v in sorted(set(spxs))})
    R_b = max(d[2] for d in data if d[2])
    print('row/speed/sec bottom capture y', R_b)
    out = []
    for idx, ms, sb, spx2, _, _ in data:
        im = np.asarray(Image.open('run/rb%04d.png' % idx).convert('RGB')).astype(int)
        scr = im[31:599, 65:705]
        rb = bar_bottom(scr, 225, 300, 400)   # x=80 -> 225
        secb = bar_bottom(scr, 481, 300, 400)  # x=208 -> 481
        rowpx = run_up(scr[:, 225], rb) if rb else 0
        secpx = run_up(scr[:, 481], secb) if secb else 0
        np_row = round(rowpx / B * 2)
        sec = round(secpx / B)
        out.append((ms, np_row, sec, spx2))
    t0 = out[0][0]
    print(' idx   t(s)   np_row  sec  spd_px')
    for ms, r, s, sp in out:
        print('%6.2f  %5d   %5d  %3d  %5d' % ((ms - t0) / 1000.0, r, s, sp))
    # unwrap np_row (mod 64) and fit
    ts = []
    rs = []
    acc = 0
    prev = None
    for ms, r, s, sp in out:
        if prev is not None:
            d = (r - prev) % 64
            if d > 32:
                d -= 64
            acc += d
        else:
            acc = r
        prev = r
        ts.append((ms - t0) / 1000.0)
        rs.append(acc)
    ts = np.array(ts); rs = np.array(rs)
    if len(ts) > 3:
        A = np.vstack([ts, np.ones_like(ts)]).T
        slope, icpt = np.linalg.lstsq(A, rs, rcond=None)[0]
        print('row rate = %.2f rows/s (%.3f s/row)' % (slope, 1.0 / slope if slope else 0))
        print('  speed 3 -> tick rate = %.2f Hz' % (slope * 3))

if __name__ == '__main__':
    main()
