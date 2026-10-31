import numpy as np, capstone, re
from pe import *
p=PE()
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
n,va,vsz,raw,rsz=p.sec('.trace')
buf=np.frombuffer(p.m,dtype=np.uint8,count=rsz,offset=raw)
T=0x3a5a30
e8=np.nonzero(buf[:-5]==0xe8)[0]
d=buf[e8+1].astype(np.int64)|(buf[e8+2].astype(np.int64)<<8)|(buf[e8+3].astype(np.int64)<<16)|(buf[e8+4].astype(np.int64)<<24)
d=np.where(d>=2**31,d-2**32,d)
hits=e8[(e8+va+5+d)==T]
out=open('senders.txt','w')
for h in hits:
    a=int(h)+va
    code=p.read(a-0x80,0x80)
    ins=list(md.disasm(code,BASE+a-0x80))
    op=None
    for i in ins:
        m=re.match(r'mov (e[a-z]+|r\d+d), (0x[0-9a-f]+)$', i.mnemonic+' '+i.op_str)
        if m and 0x100<=int(m.group(2),16)<0x2000: op=int(m.group(2),16)
        m2=re.match(r'mov word ptr \[r[bs]p [+-] 0x[0-9a-f]+\], (0x[0-9a-f]+)$', i.mnemonic+' '+i.op_str)
        if m2 and 0x100<=int(m2.group(1),16)<0x2000: op=int(m2.group(1),16)
    out.write(f"{a:#x}\t{op:#06x}\n" if op else f"{a:#x}\tNONE\n")
print(len(hits))
