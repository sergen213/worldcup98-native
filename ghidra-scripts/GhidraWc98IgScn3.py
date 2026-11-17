# Pass-21 IgScn3: IGMENU handler cluster.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

program = currentProgram
fm = program.getFunctionManager()
addrf = program.getAddressFactory()
listing = program.getListing()

dec = DecompInterface()
dec.openProgram(program)
mon = ConsoleTaskMonitor()


def toA(a):
    return addrf.getAddress("0x%08x" % a)


def deco(f):
    res = dec.decompileFunction(f, 120, mon)
    if res is None:
        return None
    d = res.getDecompiledFunction()
    if d is None:
        return None
    c = d.getC()
    if c is None:
        return None
    if isinstance(c, str):
        return c
    return c.getBodyAsString()


def dump(va, label):
    f = fm.getFunctionAt(toA(va))
    if f is None:
        f = fm.getFunctionContaining(toA(va))
    print("=== %s at 0x%x -> %s" % (label, va, f.getName() if f else "MISSING"))
    if f:
        txt = deco(f)
        if txt:
            for line in txt.splitlines():
                print("   | " + line)


dump(0x43FC40, "IGMENU_TICK")
dump(0x440830, "IGMENU_KEY")

it = program.getReferenceManager().getReferencesTo(toA(0x4461B4))
seen = set()
while it.hasNext():
    ref = it.next()
    fromA = ref.getFromAddress()
    f = fm.getFunctionContaining(fromA)
    nm = f.getName() if f else "?"
    ep = int(f.getEntryPoint().toString(), 16) if f else 0
    if (nm, ep) not in seen:
        seen.add((nm, ep))
        print("   CALLSITE %s from %s in %s" % (ref, fromA, nm))
