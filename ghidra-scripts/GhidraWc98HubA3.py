# Pass-44 HubA3: create an in-memory function for the hub message-dispatch body and
# decompile it, so wedge tag -> scene routing is read from pseudocode (endianness-correct
# fourcc decode: packed as first char in the high byte).
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.address import AddressSet
from ghidra.program.model.symbol import SourceType

program = currentProgram
fm = program.getFunctionManager()
addrf = program.getAddressFactory()
dec = DecompInterface()
dec.openProgram(program)
mon = ConsoleTaskMonitor()


def toA(a):
    return addrf.getAddress("0x%08x" % a)


specs = [
    (0x449C04, 0x44A600, "HUB_DISPATCH"),
    (0x4455B4, 0x445D00, "HUB_PRODUCER"),
]
for entry, end, nm in specs:
    ea = toA(entry)
    f = fm.getFunctionAt(ea)
    if f is None:
        try:
            src = getattr(SourceType, "USER_DEFINED", None) or getattr(SourceType, "USER_DEFINE")
            fm.createFunction(nm, ea, AddressSet(ea, toA(end)), src)
        except Exception as e:
            print("create %s failed: %s" % (nm, e))
    f = fm.getFunctionAt(ea)
    if f is None:
        print("no function at %s" % nm)
        continue
    print("=== %s %s ===" % (nm, f.getEntryPoint()))
    df = dec.decompileFunction(f, 120000, mon)
    if df is None or df.getDecompiledFunction() is None:
        print("decompile failed")
        continue
    print(df.getDecompiledFunction().getC())
