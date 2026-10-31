import sys, importlib.util
import os
TOOLS=os.environ.get('CD_TOOLS', os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'))
spec=importlib.util.spec_from_file_location('cdextract',os.path.join(TOOLS,'cdextract.py'))
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
from pathlib import Path
G=Path(sys.argv[1])
for d in sorted(G.iterdir()):
    p=d/'0.pamt'
    if p.exists():
        try:
            for e in m.parse_pamt(p): print(d.name, e[0], e[4])
        except Exception as ex: print(d.name,'ERR',ex, file=sys.stderr)
