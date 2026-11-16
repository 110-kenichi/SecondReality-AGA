import re
from pathlib import Path

src = Path(r"reference/SecondReality/COMAN/THELOOP.INC").read_text(encoding='latin-1').splitlines()
lines = []
for raw in src:
    l = raw.split(';')[0].strip()
    if l:
        lines.append(l)

i = lines.index('_@seek0:')
blocks = []
cur = None
for l in lines[i:]:
    m = re.match(r'_@seek(\d+):$', l)
    if m:
        if cur:
            blocks.append(cur)
        cur = {'i': int(m.group(1)), 'mode': None, 'bx_add': None,
               'lea': None, 'eax': [], 'ecx': [], 'n_mov': 0}
        continue
    if cur is None:
        continue
    ll = l.lower()
    if ll == 'theloop endp':
        blocks.append(cur)
        cur = None
        break
    if ll.startswith('add si,'):
        cur['mode'] = 1 if ll.endswith('xsina1') else 2
    elif ll.startswith('add bx,-'):
        cur['bx_add'] = -int(ll.split(',')[1])
    elif ll.startswith('lea dx,'):
        cur['lea'] = int(re.search(r'\+(-?\d+)', ll).group(1))
    elif ll.startswith('add eax,') and 'ecx' not in ll:
        cur['eax'].append(int(ll.split(',')[1].rstrip('h'), 16))
    elif ll.startswith('add ecx,'):
        cur['ecx'].append(int(ll.split(',')[1].rstrip('h'), 16))
    elif ll.startswith('mov es:'):
        cur['n_mov'] += 1
if cur:
    blocks.append(cur)

print('blocks', len(blocks))
print('modes', {m: sum(1 for b in blocks if b['mode'] == m) for m in (1, 2)})
print('blocks without lea', sum(1 for b in blocks if b['lea'] is None))
for b in blocks[:6] + blocks[60:64] + blocks[-3:]:
    print('block %3d mode=%s bx=%s lea=%s eax=%s ecx=%s mov=%d' % (
        b['i'], b['mode'], b['bx_add'], b['lea'],
        [hex(v) for v in b['eax']], [hex(v) for v in b['ecx']], b['n_mov']))
