# GhidraWc98IgBound1: remap table bound grammar. READ-ONLY.
# 1) decompile FUN_0043c9a0 (remap accessor) + every function referencing DAT_005774a4
# 2) dump dword table at 0x5774a4 indices 0..5247 (full possible span for ids<=5082)
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

program = currentProgram
af = program.getAddressFactory()
fm = program.getFunctionManager()
listing = program.getListing()
mem = program.getMemory()
decomp = DecompInterface(); decomp.openProgram(program)
mon = ConsoleTaskMonitor()
import sys; sys.stdout.reconfigure(line_buffering=True)

def A(v): return af.getAddress("0x%x" % v)

TABLE = 0x5774a4
N = 5248

print("=== TABLE %d dwords" % N)
words = []
for i in range(N):
    words.append(mem.getInt(A(TABLE + 4 * i)))
# print rows for ids that appear in IGOPTN2.LAY rows (4096..5247 window)
for i in range(4096, N):
    print("R %d 0x%08x" % (i, words[i]))

for ep in (0x43c9a0, 0x4c31d0):
    f = fm.getFunctionContaining(A(ep))
    if f is None:
        continue
    df = decomp.decompileFunction(f, 180, mon).getDecompiledFunction()
    print("=== DECOMP %s 0x%x\n%s" % (f.getName(), ep, df.getC() if df else "FAIL"))
