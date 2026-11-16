"""Extract the reference block table (kBlocks) from the SecondReality++ port."""
import re
from pathlib import Path

src = Path('run/COMAN_Data.cpp').read_text(encoding='utf-8', errors='replace')
i = src.index('const TheLoopBlock kBlocks[]')
j = src.index('};', i)
body = src[i:j]

rows = re.findall(r'\{\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(0x[0-9A-Fa-f]+u?|\d+u?)\s*\}', body)
print('blocks', len(rows))

out = ['/* generated from SecondReality++ COMAN_Data.cpp */',
       '#ifndef KBLOCKS_H', '#define KBLOCKS_H', '',
       'struct TheLoopBlock { unsigned char mode; short bx_add; short dx_add; unsigned int eax_inc; };',
       '', 'static const struct TheLoopBlock kBlocks[] = {']
for mode, bx, dx, inc in rows:
    out.append('    { %s, %s, %s, %sU },' % (mode, bx, dx, inc.rstrip('uU')))
out.append('};')
out.append('#define KBLOCKS_SIZE %d' % len(rows))
out.append('#endif')
Path('run/kblocks.h').write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')
print('wrote run/kblocks.h')
