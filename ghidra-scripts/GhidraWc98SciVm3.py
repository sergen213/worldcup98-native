# SCI VM pass 3: decompile FUN_004ab110 (scene-block fill), block helpers, new region funcs;
# disassemble+create funcs in 0x4a9b00..0x4aa148 and decompile.
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
    s = a if isinstance(a, str) else "0x%x" % a
    return af.getAddress(s)

out = []
def decomp(t):
    a = gaddr(t)
    f = fm.getFunctionAt(a) or fm.getFunctionContaining(a)
    name = f.getName() if f else "?"
    if f is None:
        out.append("NO FUNC %s" % t); return
    r = deci.decompileFunction(f, 90, mon)
    cf = r.getDecompiledFunction()
    out.append("=== %s (%s) ===" % (t, name))
    out.append(cf.getC() if cf else "<fail>")

for t in (0x4ab110, 0x496200, 0x4962d0, 0x4963b0, 0x496400, 0x495aa0, 0x495bb0):
    decomp(t)

a = 0x4a9b00
while a <= 0x4aa148:
    DisassembleCommand(gaddr(a), None, True).applyTo(prog)
    a += 1
starts = []
a = 0x4a9b00
while a <= 0x4aa150:
    f = fm.getFunctionAt(gaddr(a))
    if f is not None:
        starts.append("0x%x" % a)
    a += 1
out.append("starts 4a9b00..4aa150: %r" % starts)
for s in starts:
    decomp(s)

for t in (0x4ab4a0, 0x4ab520, 0x4ab650, 0x4ab6a0, 0x4ab5d0):
    decomp(t)

open(OUT + "scivm3.txt", "w").write("\n".join(out))
print("WROTE scivm3.txt")
