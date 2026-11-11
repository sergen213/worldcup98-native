# PyGhidra pass: decompile WC98 ball physics/collision bodies (D-14/D-10).
# Pure JPype API; never import ghidra.program.flatapi.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.address import GenericAddress

prog = currentProgram
out = {}


def decompile(addr):
    a = prog.getAddressFactory().getAddress("0x%08x" % addr)
    fm = prog.getFunctionManager()
    f = fm.getFunctionAt(a)
    if f is None:
        f = fm.getFunctionContaining(a)
    if f is None:
        return "NO FUNCTION AT 0x%x (create function first)" % addr
    d = DecompInterface()
    d.openProgram(prog)
    r = d.decompileFunction(f, 120, ConsoleTaskMonitor())
    df = r.getDecompiledFunction()
    if df is None:
        return "DECOMPILE FAILED 0x%x (%s)" % (addr, r.getErrorMessage())
    return df.getC()


targets = {
    "tick_004b3320": 0x4b3320,
    "kernel_004b2090": 0x4b2090,
    "companion_004b2a40": 0x4b2a40,
    "spin_004b1f30": 0x4b1f30,
    "drag_004b2f80": 0x4b2f80,
    "attach_004b1d40": 0x4b1d40,
    "predictraw_004b3590": 0x4b3590,
    "collidestep_004c0150": 0x4c0150,
    "resolvecollision_004c0997": 0x4c0997,
    "golamouth_004c159c": 0x4c159c,
}

import os
os.makedirs("/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE", exist_ok=True)
for name, addr in targets.items():
    txt = decompile(addr)
    path = "/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE/%s.txt" % name
    with open(path, "w") as fh:
        fh.write(txt)
    print(name, len(txt))
