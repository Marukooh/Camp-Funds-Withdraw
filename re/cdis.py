import sys, capstone
from pe import *
p = PE()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
STR = {}
def strat(rva):
    try:
        o = p.off(rva)
        if o is None: return None
        b = p.m[o:o+120]
        z = b.find(b'\0')
        if z >= 4 and all(32 <= c < 127 for c in b[:z]): return b[:z].decode()
    except Exception: pass
    return None
def funcstart(rva):
    r = rva
    while r > rva - 0x20000:
        if p.read(r-1,1) in (b'\xcc',) and p.read(r-2,1) in (b'\xcc', b'\xc3'):
            return r
        r -= 1
    return None
def dis(start, n=400, stop_ret=True):
    code = p.read(start, n*8)
    out = []
    for ins in md.disasm(code, BASE+start):
        rva = ins.address - BASE
        s = f"{rva:08x}: {ins.mnemonic} {ins.op_str}"
        if 'rip' in ins.op_str:
            import re
            m = re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]', ins.op_str)
            if m:
                d = int(m.group(2),16) * (1 if m.group(1)=='+' else -1)
                t = rva + ins.size + d
                st = strat(t)
                s += f"   ; {t:#x}" + (f' "{st}"' if st else '')
        if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
            s += f"   ; sub_{int(ins.op_str,16)-BASE:x}"
        out.append(s)
        if len(out) >= n: break
        if stop_ret and ins.mnemonic in ('ret','int3') and len(out) > 5 and ins.mnemonic=='int3': break
    return out
if __name__ == '__main__':
    a = int(sys.argv[1],16)
    n = int(sys.argv[2]) if len(sys.argv)>2 else 300
    if len(sys.argv)>3 and sys.argv[3]=='f':
        a = funcstart(a); print('func start', hex(a))
    print('\n'.join(dis(a, n)))
