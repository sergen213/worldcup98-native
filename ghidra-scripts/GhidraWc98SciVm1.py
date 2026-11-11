# SCI VM pass 1 (pass17sci): decompile scheduler pair, registration chain, scene-install;
# enumerate xrefs to scene-block base / descriptor array / handler tables.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
refmgr = prog.getReferenceManager()
deci = DecompInterface(); deci.openProgram(prog)
mon = ConsoleTaskMonitor()
OUT = "analysis/pass17sci/DECOMPILE/"

def fnof(addr):
    f = fm.getFunctionContaining(af.getAddress(addr))
    return f.getName() if f else None

out = []
# 1. decompiles
for t in ("0x4a0d80", "0x4a0e50", "0x49f5a0", "0x4aa148", "0x4aa1ba", "0x4aa267",
          "0x4aa2fc", "0x4aa485"):
    a = af.getAddress(t)
    f = fm.getFunctionAt(a) or fm.getFunctionContaining(a)
    if f is None:
        out.append("NO FUNC %s" % t); continue
    r = deci.decompileFunction(f, 90, mon)
    cf = r.getDecompiledFunction()
    out.append("=== %s (%s) ===" % (t, f.getName()))
    out.append(cf.getC() if cf else "<decompile failed>")

# 2. xrefs to key DATs
for da in ("0x63697c", "0x580a9c", "0x580520", "0x57fc40", "0x57fbc8", "0x580df0",
           "0x580de8", "0x641594", "0x57fb7c"):
    a = af.getAddress(da)
    out.append("=== XREFS TO %s ===" % da)
    for r in refmgr.getReferencesTo(a):
        fr = r.getFromAddress()
        out.append("  from %s (%s) type %s -> to %s" % (fr, fnof(str(fr)), r.getReferenceType(), r.getToAddress()))

open(OUT + "scivm1.txt", "w").write("\n".join(out))
print("WROTE scivm1.txt lines=%d" % len(out))
