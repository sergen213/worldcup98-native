# SCI VM pass 6: disassemble scene-module region 0x4a97c0..0x4ab750, create all functions,
# decompile each fully into sci_module.txt.
from ghidra.app.decompiler import DecompInterface
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
refmgr = prog.getReferenceManager()
deci = DecompInterface(); deci.openProgram(prog)
mon = ConsoleTaskMonitor()
OUT = "analysis/pass17sci/DECOMPILE/"

def gaddr(a):
    return af.getAddress("0x%x" % (a if isinstance(a, int) else int(a, 16)))

def vi(addr):
    s = str(addr)
    try:
        return int(s, 16)
    except ValueError:
        return -1

lo, hi = 0x4a97c0, 0x4ab750
a = lo
while a <= hi:
    DisassembleCommand(gaddr(a), None, True).applyTo(prog)
    a += 1

cands = set()
a = lo
while a <= hi:
    for r in refmgr.getReferencesFrom(gaddr(a)):
        t = vi(r.getToAddress())
        if lo <= t <= hi:
            cands.add(t)
    a += 1
for s in (0x4a9830, 0x4aa148):
    cands.add(s)
for t in sorted(cands):
    if fm.getFunctionAt(gaddr(t)) is None:
        CreateFunctionCmd(gaddr(t)).applyTo(prog)

fns = []
fs = fm.getFunctions(True)
while fs.hasNext():
    f = fs.next()
    v = vi(f.getEntryPoint())
    if lo <= v <= hi:
        fns.append(v)
print("region functions:", len(fns))

out = ["region funcs: %r" % [hex(x) for x in sorted(fns)]]
for v in sorted(fns):
    f = fm.getFunctionAt(gaddr(v))
    r = deci.decompileFunction(f, 90, mon)
    cf = r.getDecompiledFunction()
    out.append("=== %s (%s) ===" % (hex(v), f.getName()))
    out.append(cf.getC() if cf else "<fail>")
open(OUT + "sci_module.txt", "w").write("\n".join(out))
print("WROTE sci_module.txt")
