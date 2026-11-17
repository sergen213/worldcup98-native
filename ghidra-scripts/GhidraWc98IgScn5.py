# Pass-21 IgScn5: ingame cluster, CORRECTED VAs (VA = raw + 0x400C00).
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
    (0x47882C, "WSET_ID_TAG"),
    (0x4788B4, "WSET_PTR_TAG"),
    (0x47A3E8, "RES_LOOKUP"),
    (0x472BC0, "WLIST_OP"),
    (0x472AC0, "WLIST_ADD"),
    (0x43FFE8, "COMMON_TICK"),
    (0x446598, "SCENE_REGISTER"),
    (0x43A3F4, "SCENE_SWITCH_NAME"),
    (0x439D38, "MATCH_RESUME"),
    (0x4408AC, "IGOPTN_TICK"),
    (0x440944, "IGOPTN_INIT"),
    (0x4409F8, "IGSEL_TICK"),
    (0x440790, "IGOPTN_TICKA"),
):
    dump(va, label)
