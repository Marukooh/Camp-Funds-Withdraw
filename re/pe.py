import mmap, struct, re, sys
import os
EXE = os.environ.get("CD_EXE", os.path.expanduser("~/.local/share/Steam/steamapps/common/Crimson Desert/bin64/CrimsonDesert.exe"))
BASE = 0x140000000

class PE:
    def __init__(self, path=EXE):
        self.f = open(path, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        m = self.m
        pe = struct.unpack_from("<I", m, 0x3C)[0]
        nsec = struct.unpack_from("<H", m, pe + 6)[0]
        opt = struct.unpack_from("<H", m, pe + 20)[0]
        so = pe + 24 + opt
        self.secs = []
        for i in range(nsec):
            name, vsz, va, rsz, raw = struct.unpack_from("<8sIIII", m, so + 40 * i)
            self.secs.append((name.rstrip(b"\0").decode(), va, vsz, raw, rsz))

    def sec(self, name):
        for s in self.secs:
            if s[0] == name:
                return s

    def off(self, rva):
        for n, va, vsz, raw, rsz in self.secs:
            if va <= rva < va + rsz:
                return raw + rva - va
        return None

    def rva(self, off):
        for n, va, vsz, raw, rsz in self.secs:
            if raw <= off < raw + rsz:
                return va + off - raw
        return None

    def read(self, rva, n):
        o = self.off(rva)
        return self.m[o:o + n]

    def u32(self, rva): return struct.unpack("<I", self.read(rva, 4))[0]
    def u64(self, rva): return struct.unpack("<Q", self.read(rva, 8))[0]

    def find_all(self, needle, secname=None, align=1):
        if secname:
            n, va, vsz, raw, rsz = self.sec(secname)
            lo, hi = raw, raw + rsz
        else:
            lo, hi = 0, len(self.m)
        res = []
        p = self.m.find(needle, lo, hi)
        while p != -1:
            if p % align == 0:
                res.append(self.rva(p))
            p = self.m.find(needle, p + 1, hi)
        return res

    def cstr(self, rva, maxn=256):
        b = self.read(rva, maxn)
        return b.split(b"\0")[0].decode("latin1")

    def vtables_for(self, mangled):
        """mangled e.g. '.?AVUIGamePlayControlRootBank@uiCommonScript@pa@@'"""
        out = []
        for name_rva in self.find_all(mangled.encode() + b"\0"):
            td = name_rva - 0x10
            for colrva in self.find_all(struct.pack("<I", td), align=4):
                col = colrva - 0xC
                sig, offs, cdoff, tdr, chd, selfr = struct.unpack("<IIIIII", self.read(col, 24))
                if sig != 1 or selfr != col:
                    continue
                for vref in self.find_all(struct.pack("<Q", BASE + col), align=8):
                    out.append((offs, vref + 8))
        return out
