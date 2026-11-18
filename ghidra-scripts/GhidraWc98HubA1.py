# Pass-44 HubA1: resolve hub wedge routing. The saved project has no function covering the
# hub handler body, so locate the enclosing function via getFunctionBefore and decompile it;
# also disassemble the raw wedge-compare region for the immediate scene targets.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

program = currentProgram
fm = program.getFunctionManager()
addrf = program.getAddressFactory()
dec = DecompInterface()
dec.openProgram(program)
mon = ConsoleTaskMonitor()


def toA(a):
    return addrf.getAddress("0x%08x" % a)


for va in (0x449c4d, 0x445a2a, 0x485e35):
    a = toA(va)
    f = fm.getFunctionBefore(a)
    if f is None:
        print("=== site 0x%08x: no function before ===" % va)
        continue
    print("=== site 0x%08x -> entry %s (%s) ===" % (va, f.getEntryPoint(), f.getName()))
    for rng in f.getBody():
        print("   body %s" % rng)
    df = dec.decompileFunction(f, 60000, mon)
    if df is None or df.getDecompiledFunction() is None:
        print("   decompile failed")
        continue
    print(df.getDecompiledFunction().getC())
