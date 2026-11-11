# SCI VM pass 5: dump pseudocode of every function in .text to all_functions.txt (resumable).
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
deci = DecompInterface(); deci.openProgram(prog)
mon = ConsoleTaskMonitor()
OUT = "analysis/pass17sci/DECOMPILE/all_functions.txt"

def vi(addr):
    try:
        return int(str(addr), 16)
    except ValueError:
        return -1

done = set()
if os.path.exists(OUT):
    for line in open(OUT):
        if line.startswith("=== FUN_"):
            done.add(line.split()[1])

n = 0
new = 0
with open(OUT, "a") as fh:
    fs = fm.getFunctions(True)
    while fs.hasNext():
        f = fs.next()
        v = vi(f.getEntryPoint())
        nm = f.getName()
        if not (0x401000 <= v < 0x560000):
            continue
        if nm in done:
            n += 1
            continue
        r = deci.decompileFunction(f, 30, mon)
        cf = r.getDecompiledFunction()
        fh.write("=== %s (%s) ===\n" % (nm, f.getEntryPoint()))
        fh.write((cf.getC() if cf else "<fail>") + "\n")
        n += 1
        new += 1
fh.close()
print("PROCESSED total=%d new=%d" % (n, new))
