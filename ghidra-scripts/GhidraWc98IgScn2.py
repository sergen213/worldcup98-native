# Pass-21 IgScn2: xrefs into the name-pointer array VA 0x57d780..0x57d870 and
# DAT_0057d7a0; plus who reads the ig scene name strings (0x564d00..0x564f00).
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

program = currentProgram
fm = program.getFunctionManager()
mem = program.getMemory()
addrf = program.getAddressFactory()
listing = program.getListing()
import java.util.Iterator
from ghidra.program.util import XReferenceUtil


def toA(a):
    return addrf.getAddress("0x%08x" % a)


def xrefs_to(va, label):
    a = toA(va)
    it = listing.getReferenceIterator(a)
    n = 0
    while it.hasNext():
        ref = it.next()
        fromA = ref.getFromAddress()
        f = fm.getFunctionContaining(fromA)
        print("%s ref %s from %s (%s)" % (label, ref, fromA, f.getName() if f else "?"))
        n += 1
    print("%s: %d refs" % (label, n))


# 1) xrefs to each name-pointer slot in the array
for i in range(0x2c):
    xrefs_to(0x57D780 + i * 4, "NAMEP%02x" % i)

# 2) xrefs to DAT_0057d7a0 (runtime pool ptr)
xrefs_to(0x57D7A0, "DAT_57d7a0")

# 3) xrefs to a few scene name strings
for va, nm in ((0x564D68, "igoption"), (0x564D5C, "ingame"), (0x564D0C, "cselect")):
    xrefs_to(va, "STR_" + nm)
