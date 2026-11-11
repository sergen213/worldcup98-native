# PyGhidra pass 2: net raycast helpers + resolveCollision (D-14).
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd

prog = currentProgram


def decompile(addr):
    a = prog.getAddressFactory().getAddress("0x%08x" % addr)
    fm = prog.getFunctionManager()
    f = fm.getFunctionAt(a)
    if f is None:
        f = fm.getFunctionContaining(a)
    if f is None:
        try:
            tx = prog.startTransaction("cf")
            CreateFunctionCmd(a).applyTo(prog)
            prog.endTransaction(tx, True)
        except Exception as e:  # noqa
            return "NO FUNCTION at 0x%x (%s)" % (addr, e)
        f = fm.getFunctionAt(a)
        if f is None:
            return "CREATE FAILED at 0x%x" % addr
    d = DecompInterface()
    d.openProgram(prog)
    d.openProgram(prog)
    r = d.decompileFunction(f, 120, ConsoleTaskMonitor())
    df = r.getDecompiledFunction()
    if df is None:
        return "DECOMPILE FAILED 0x%x" % addr
    return df.getC()


targets = {
    "netback_004bf9e0": 0x4bf9e0,
    "netside_004bfb40": 0x4bfb40,
    "netroof_004bfcc0": 0x4bfcc0,
    "netouterroof_004bfed0": 0x4bfed0,
    "netouterside_004bffd0": 0x4bffd0,
    "dist_004c2a40": 0x4c2a40,
    "atan_004f3190": 0x4f3190,
    "rngtrig_004f3db4": 0x4f3db4,
    "rest_004bf490": 0x4bf490,
    "goalcross_004bfdd0": 0x4bfdd0,
    "markgoal_004c08d0": 0x4c08d0,
    "resolvecollision_004c0997": 0x4c0997,
}

import os
os.makedirs("/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE", exist_ok=True)
for name, addr in targets.items():
    txt = decompile(addr)
    with open("/mnt/1st4TB/WorldCup98/analysis/pass16ball/DECOMPILE/%s.txt" % name, "w") as fh:
        fh.write(txt)
    print(name, len(txt))
