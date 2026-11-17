# Pass-21 IgScn4: ingame scene-handler cluster support functions.
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


def dump(va, label):
    f = fm.getFunctionAt(toA(va))
    if f is None:
        f = fm.getFunctionContaining(toA(va))
    print("=== %s at 0x%x -> %s" % (label, va, f.getName() if f else "MISSING"))
    if not f:
        return
    r = dec.decompileFunction(f, 120, mon)
    if r and r.getDecompiledFunction():
        for line in r.getDecompiledFunction().getC().splitlines():
            print(" | " + line)
    else:
        print(" | DECOMP-FAIL")


for va, label in (
    (0x445998, "SCENE_REGISTER"),
    (0x43F3E8, "COMMON_TICK"),
    (0x477C2C, "WSET_ID_TAG"),
    (0x477CB4, "WSET_PTR_TAG"),
    (0x4797E8, "WLOOKUP"),
    (0x471FC0, "WLIST_OP"),
    (0x471EC0, "WLIST_ADD"),
    (0x4408AC, "IGOPTN_TICK"),
    (0x440944, "IGOPTN_INIT"),
    (0x4409F8, "IGSEL_TICK"),
    (0x440CBC, "IGSEL_INIT"),
    (0x4397F4, "SCENE_SWITCH_A"),
    (0x439138, "MATCH_RESUME"),
):
    dump(va, label)
