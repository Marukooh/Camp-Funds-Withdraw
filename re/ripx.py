import numpy as np, sys
from pe import *
p = PE()
n, va, vsz, raw, rsz = p.sec('.trace')
buf = np.frombuffer(p.m, dtype=np.uint8, count=rsz, offset=raw)
def ripxrefs(target, maxk=4):
    # disp32 at position i, instruction end = i+4+k (k bytes of immediate)
    d = np.lib.stride_tricks.sliding_window_view(buf, 4)
    disp = (d[:,0].astype(np.int64) | (d[:,1].astype(np.int64)<<8) | (d[:,2].astype(np.int64)<<16) | (d[:,3].astype(np.int64)<<24))
    disp = np.where(disp >= 2**31, disp - 2**32, disp)
    pos = np.arange(len(disp), dtype=np.int64) + va + 4
    res = []
    for k in range(maxk+1):
        hits = np.nonzero(pos + k + disp == target)[0]
        res += [(int(h)+va, k) for h in hits]
    return sorted(res)
if __name__ == '__main__':
    for t in sys.argv[1:]:
        print(t, [(hex(a),k) for a,k in ripxrefs(int(t,16))])
