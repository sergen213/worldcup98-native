# SCI VM pass 8: xrefs to scene-module table DATs + decompile 0x4ac800..0x4ac830 family.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
refmgr = prog.getReferenceManager()
deci = DecompInterface(); deci.openProgram(prog)
mon = ConsoleTaskMonitor()
def gaddr(a):
    return af.getAddress("0x%x" % (a if isinstance(a, int) else int(a, 16)))

out = []
for da in (0x580d4c, 0x580d5c, 0x580d6c, 0x580d70, 0x580dcc, 0x580dd0, 0x580dd4,
           0x580dd8, 0x580de0, 0x580a9c):
    out.append("=== XREFS TO %s ===" % hex(da))
    for r in refmgr.getReferencesTo(gaddr(da)):
        fr = r.getFromAddress()
        f = fm.getFunctionContaining(fr)
        out.append("  from %s (%s) type %s" % (fr, f.getName() if f else None, r.getReferenceType()))

for t in (0x4ac800, 0x4ac810, 0x4ac820, 0x4ac830):
    a = gaddr(t)
    f = fm.getFunctionAt(a) or fm.getFunctionContaining(a)
    if f is None:
        out.append("NO FUNC %s" % hex(t)); continue
    r = deci.decompileFunction(f, 90, mon)
    cf = r.getDecompiledFunction()
    out.append("=== %s (%s) ===" % (hex(t), f.getName()))
    out.append(cf.getC() if cf else "<fail>")
open("analysis/pass17sci/DECOMPILE/scivm8.txt", "w").write("\n".join(out))
print("WROTE scivm8")
