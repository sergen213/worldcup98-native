# PyGhidra pass 7: collideGoalMouth tail function.
from ghidra.app.decompiler import DecompInterface
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram


def ga(v):
    return prog.getAddressFactory().getAddress("0x%08x" % v)


fm = prog.getFunctionManager()
f = fm.getFunctionAt(ga(0x4C159C))
if f is None:
    tx = prog.startTransaction("pass7")
    CreateFunctionCmd(ga(0x4C159C)).applyTo(prog)
    prog.endTransaction(tx, True)
    f = fm.getFunctionAt(ga(0x4C159C))
if f is None:
    print("goalmouth: create failed")
else:
    d = DecompInterface()
    d.openProgram(prog)
    r = d.decompileFunction(f, 120, ConsoleTaskMonitor())
    df = r.getDecompiledFunction()
    txt = "FAILED" if df is None else df.getC()
    with open("/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE/goalmouth_004c159c.txt", "w") as fh:
        fh.write(txt)
    print("goalmouth", len(txt))
