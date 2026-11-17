# Pass-21 IgScn6: create functions (in-memory only, NO save) at ingame scene
# handler entries, then decompile.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.flatapi import FlatProgramAPI

program = currentProgram
fm = program.getFunctionManager()
addrf = program.getAddressFactory()
api = FlatProgramAPI(program, ConsoleTaskMonitor())

dec = DecompInterface()
dec.openProgram(program)
mon = ConsoleTaskMonitor()


def toA(a):
    return addrf.getAddress("0x%08x" % a)


for va, nm in (
    (0x440790, "IGOPTN_TICKA"),
    (0x440832, "IGOPTN_KEYA"),
    (0x4408AD, "IGOPTN_TICKB"),
    (0x440944, "IGOPTN_INITB"),
    (0x4409F8, "IGSEL_TICK"),
):
    a = toA(va)
    try:
        api.disassemble(a)
    except Exception as e:
        print("DISASM %08x: %s" % (va, e))
    f = fm.getFunctionAt(a)
    if f is None:
        try:
            f = api.createFunction(a, nm)
        except Exception as e:
            print("CREATE %08x FAIL: %s" % (va, e))
            f = None
    print("=== %s at 0x%x -> %s" % (nm, va, f.getName() if f else "MISSING"))
    if f:
        r = dec.decompileFunction(f, 120, mon)
        if r and r.getDecompiledFunction():
            for line in r.getDecompiledFunction().getC().splitlines():
                print(" | " + line)
        else:
            print(" | DECOMP-FAIL")
