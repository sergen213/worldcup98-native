# PyGhidra pass 6: post/bar bounce placement helpers + netDamp tail helper.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog = currentProgram


def ga(v):
    return prog.getAddressFactory().getAddress("0x%08x" % v)


for name, addr in (("postbounce_004bf660", 0x4BF660), ("barbounce_004bf760", 0x4BF760),
                   ("netdamp_004c12e0", 0x4C12E0)):
    f = prog.getFunctionManager().getFunctionAt(ga(addr))
    if f is None:
        print(name, "NO FUNCTION")
        continue
    d = DecompInterface()
    d.openProgram(prog)
    r = d.decompileFunction(f, 120, ConsoleTaskMonitor())
    df = r.getDecompiledFunction()
    txt = "FAILED" if df is None else df.getC()
    with open("/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE/%s.txt" % name, "w") as fh:
        fh.write(txt)
    print(name, len(txt))
