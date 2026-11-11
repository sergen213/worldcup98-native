# SCI VM pass 2 (pass17sci): decompile DAT_0063697c scene-block family + loader-chain callers;
# disassemble unanalyzed region 0x4ab300..0x4ab650, create funcs at xref sites, decompile.
from ghidra.app.decompiler import DecompInterface
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
refmgr = prog.getReferenceManager()
listing = prog.getListing()
deci = DecompInterface(); deci.openProgram(prog)
mon = ConsoleTaskMonitor()
OUT = "analysis/pass17sci/DECOMPILE/"
gaddr = lambda a: af.getAddress("0x%x" % a)

out = []
def decomp(t, label=None):
    a = gaddr(t if isinstance(t, int) else int(t, 16))
    f = fm.getFunctionAt(a) or fm.getFunctionContaining(a)
    if f is None:
        out.append("NO FUNC %s" % t); return
    r = deci.decompileFunction(f, 90, mon)
    cf = r.getDecompiledFunction()
    out.append("=== %s (%s) ===" % (t, f.getName()))
    out.append(cf.getC() if cf else "<fail>")

for t in (0x495bd0, 0x495c70, 0x495d10, 0x495e00, 0x495f70, 0x495920, 0x496090,
          0x4aae90, 0x4aacf0):
    decomp(t)

# callers of loader chain
for t in (0x4396e0, 0x443714, 0x4c2dc0):
    f = fm.getFunctionAt(gaddr(t))
    out.append("=== CALLERS OF %s (%s) ===" % (hex(t), f.getName() if f else "?"))
    if f:
        for r in refmgr.getReferencesTo(f.getEntryPoint()):
            cf = fm.getFunctionContaining(r.getFromAddress())
            out.append("  from %s (%s)" % (r.getFromAddress(), cf.getName() if cf else None))

# disassemble unanalyzed region and create functions at pointer targets of code flow
a = 0x4ab300
while a <= 0x4ab650:
    DisassembleCommand(gaddr(a), None, True).applyTo(prog)
    a += 1
for s in (0x4ab45c,):
    f = fm.getFunctionContaining(gaddr(s))
    out.append("FUNC containing %x -> %s" % (s, f.getName() if f else None))
# create functions for all function-start candidates in region via listing
a = 0x4ab300
made = []
while a <= 0x4ab650:
    fn = fm.getFunctionAt(gaddr(a))
    if fn is not None:
        made.append((hex(a), fn.getName()))
    a += 1
out.append("function starts in region: %r" % made)
for t in (0x4ab300,):
    pass

open(OUT + "scivm2.txt", "w").write("\n".join(out))
print("WROTE scivm2.txt")
