import re, struct, capstone
from pe import *
from xref import xrefs
p=PE()
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
n,va,vsz,raw,rsz=p.sec('.reloc')
data=p.m[raw:raw+rsz]
out=open('opmap.txt','w')
for mt in re.finditer(rb'\.\?AV(Troc[A-Za-z0-9_]+)@pa@@\0', data):
    name=mt.group(1).decode()
    try:
        vts=p.vtables_for('.?AV%s@pa@@'%name)
    except Exception as e:
        continue
    for o,vt in vts:
        for x in xrefs(vt):
            ins=list(md.disasm(p.read(x,20),BASE+x,2))
            if len(ins)==2 and ins[1].mnemonic=='mov' and 'rip' in ins[1].op_str:
                d=int(ins[1].op_str.split('+ ')[1].split(']')[0],16)
                g=x+ins[0].size+ins[1].size+d
                op=struct.unpack('<H',p.read(g+0xc,2))[0]
                out.write(f"{name}\t{op:#06x}\t{g:#x}\t{p.u64(vt+16)-BASE:#x}\n"); out.flush()
