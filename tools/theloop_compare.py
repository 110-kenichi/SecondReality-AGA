"""Compare our THELOOP.INC translation against the SecondReality++ block model."""
import re
from pathlib import Path

# ---- our parse ----
src = Path(r"reference/SecondReality/COMAN/THELOOP.INC").read_text(encoding='latin-1').splitlines()
lines = [l.split(';')[0].strip() for l in src]
lines = [l for l in lines if l]
i = lines.index('_@seek0:')
mine = []
cur = None
for l in lines[i:]:
    ll = l.lower()
    if ll == 'theloop endp':
        break
    m = re.match(r'_@seek(\d+):$', l)
    if m:
        cur = {'i': int(m.group(1)), 'mode': None, 'bx': None, 'dx': None, 'eax': None}
        mine.append(cur)
        continue
    if cur is None:
        continue
    if ll.startswith('add si,'):
        cur['mode'] = 1 if ll.endswith('xsina1') else 2
    elif ll.startswith('add bx,-'):
        cur['bx'] = -int(ll.split(',')[1])
    elif ll.startswith('lea dx,'):
        cur['dx'] = int(re.search(r'\+(-?\d+)', ll).group(1))
    elif ll.startswith('add eax,') and 'ecx' not in ll and cur['eax'] is None:
        cur['eax'] = int(ll.split(',')[1].rstrip('h'), 16)
if cur:
    mine.append(cur)

# ---- theirs ----
data = Path('run/COMAN_Data.cpp').read_text(encoding='utf-8', errors='replace')
k = data.index('const TheLoopBlock kBlocks[]')
body = data[k:data.index('};', k)]
rows = re.findall(r'\{\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(0x[0-9A-Fa-f]+u?|\d+u?)\s*\}', body)
theirs = [{'mode': int(m), 'bx': int(b), 'dx': int(d), 'eax': int(e.rstrip('uU'), 0) & 0xFFFFFFFF}
          for m, b, d, e in rows]

print('mine %d, theirs %d' % (len(mine), len(theirs)))
mism = 0
for idx, (a, b) in enumerate(zip(mine, theirs)):
    for f in ('mode', 'bx', 'dx', 'eax'):
        if a[f] != b[f]:
            if mism < 20:
                print('block %3d %s: mine=%s theirs=%s' % (idx, f, a[f], b[f]))
            mism += 1
print('mismatches', mism)
