# SCI VM pass 4: xrefs to the six DGROUP relocation-pointer targets from .SCI headers,
# full loader-chain decompiles (0x4396e0, 0x443714, 0x4437e0, 0x4c2dc0), and descriptor-adjacent
# DAT reads in [0x580a00..0x580b40].
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
refmgr = prog.getReferenceManager()
deci = DecompInterface(); deci.openProgram(prog)
mon = ConsoleTaskMonitor()
OUT = "analysis/pass17sci/DECOMPILE/"
def gaddr(s):
    return af.getAddress("0x%x" % (s if isinstance(s, int) else int(s, 16)))
out = []

for da in (0x580054, 0x580154, 0x5a31e4, 0x582054, 0x58c054):
    out.append("=== XREFS TO %s ===" % hex(da))
    for r in refmgr.getReferencesTo(gaddr(da)):
        fr = r.getFromAddress()
        f = fm.getFunctionContaining(fr)
        out.append("  from %s (%s) type %s" % (fr, f.getName() if f else None, r.getReferenceType()))

for t in (0x4396e0, 0x443714, 0x4437e0):
    f = fm.getFunctionAt(gaddr(t))
    r = deci.decompileFunction(f, 90, mon)
    cf = r.getDecompiledFunction()
    out.append("=== %s (%s) ===" % (hex(t), f.getName()))
    out.append(cf.getC() if cf else "<fail>")

# scan references to every 4-byte-aligned DAT in descriptor band [0x580a90,0x580b60]
out.append("=== XREFS SCAN 0x580a90..0x580b60 ===")
for da in range(0x580a90, 0x580b64, 4):
    rs = list(refmgr.getReferencesTo(gaddr(da)))
    if rs:
        for r in rs:
            fr = r.getFromAddress()
            f = fm.getFunctionContaining(fr)
            out.append("  %s from %s (%s) %s" % (hex(da), fr, f.getName() if f else None, r.getReferenceType()))

open(OUT + "scivm4.txt", "w").write("\n".join(out))
print("WROTE scivm4.txt")
