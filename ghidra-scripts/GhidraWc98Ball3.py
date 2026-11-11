# PyGhidra pass 3: create + decompile resolveCollision at 0x4c0997 (unanalyzed blob).
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.app.cmd.disassemble import DisassembleCommand

prog = currentProgram


def ga(v):
    return prog.getAddressFactory().getAddress("0x%08x" % v)
tx = prog.startTransaction("pass")
a = 0x4C0975
while a < 0x4C09A0:
    try:
        cmd = DisassembleCommand(ga(a), None, False)
        cmd.applyTo(prog)
    except Exception as e:  # noqa
        print("dis fail", hex(a), e)
        break
    a += 1
CreateFunctionCmd(ga(0x4C0975)).applyTo(prog)
prog.endTransaction(tx, True)

f = prog.getFunctionManager().getFunctionAt(ga(0x4C0975))
if f is None:
    print("resolveCollision: create failed")
else:
    d = DecompInterface()
    d.openProgram(prog)
    r = d.decompileFunction(f, 120, ConsoleTaskMonitor())
    df = r.getDecompiledFunction()
    txt = "FAILED" if df is None else df.getC()
    with open("/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE/resolvecollision_004c0975.txt", "w") as fh:
        fh.write(txt)
    print("resolveCollision", len(txt))
