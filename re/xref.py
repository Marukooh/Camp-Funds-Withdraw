import re, struct, sys, pickle, os
from pe import *
p = PE()
IDX = os.path.join(os.path.dirname(__file__), 'leaidx.pkl')
def build():
    n, va, vsz, raw, rsz = p.sec('.trace')
    data = p.m[raw:raw+rsz]
    d = {}
    for mt in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', data):
        i = mt.start()
        disp = struct.unpack_from('<i', data, i+3)[0]
        tgt = va + i + 7 + disp
        d.setdefault(tgt, []).append(va + i)
    return d
if os.path.exists(IDX):
    d = pickle.load(open(IDX,'rb'))
else:
    d = build(); pickle.dump(d, open(IDX,'wb'))
def xrefs(rva): return d.get(rva, [])
if __name__ == '__main__':
    S = open(os.path.join(os.path.dirname(__file__),'..','strings.txt')).read().splitlines()
    for s in sys.argv[1:]:
        for line in S:
            off, _, txt = line.strip().partition(' ')
            if txt == s:
                r = p.rva(int(off,16))
                print(s, hex(r), [hex(x) for x in xrefs(r)])
