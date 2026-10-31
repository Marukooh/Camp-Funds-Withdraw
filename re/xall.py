import sys, importlib.util
from pathlib import Path
import os
TOOLS=os.environ.get('CD_TOOLS', os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'))
spec=importlib.util.spec_from_file_location('cddecrypt',os.path.join(TOOLS,'cddecrypt.py'))
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
ce=m.cdextract
shard,out=Path(sys.argv[1]),Path(sys.argv[2]); needles=sys.argv[3:]
out.mkdir(parents=True,exist_ok=True)
for path,paz,offset,comp,orig,compression in ce.parse_pamt(shard/'0.pamt'):
    if not any(n in path for n in needles): continue
    dst=out/Path(path).name
    if dst.exists(): continue
    with open(shard/f'{paz}.paz','rb') as h: h.seek(offset); blob=h.read(comp)
    data=None
    for fn in (lambda: m.decode(blob,path,orig,compression), lambda: ce.lz4_block(blob,orig) if compression==2 else blob):
        try:
            d=fn()
            if len(d)==orig: data=d; break
        except Exception: pass
    if data is None: print('FAIL',path); continue
    dst.write_bytes(data)
