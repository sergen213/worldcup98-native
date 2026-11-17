# Pass-21 IgP2: raw disassembly of the IG popup painter/tick regions
# 0x4409d0..0x440af8 (audio popup tick), 0x441800..0x441a00 (game popup tick),
# 0x441140..0x441168 (trivia tail head). Resolve 0x56xxxx string immediates.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

program = currentProgram
listing = program.getListing()
mem = program.getMemory()
addrf = program.getAddressFactory()
fm = program.getFunctionManager()


def A(x):
    return addrf.getAddress("0x%08x" % x)


def sread(va):
    try:
        a = A(va)
        out = []
        i = 0
        while i < 96:
            b = mem.getByte(a.add(i)) & 0xFF
            if b == 0:
                break
            if b < 32 or b > 126:
                return None
            out.append(chr(b))
            i += 1
        return "".join(out)
    except Exception:
        return None


def dump(lo, hi):
    print("=== DISASM 0x%08x..0x%08x" % (lo, hi))
    addr = A(lo)
    end = A(hi)
    while addr is not None and addr.compareTo(end) < 0:
        ins = listing.getInstructionAt(addr)
        if ins is None:
            print("%08x: (no ins)" % addr.getOffset())
            addr = addr.add(1)
            continue
        s = ins.toString()
        print("%08x: %s" % (addr.getOffset(), s))
        # resolve immediate string VAs in this instruction
        for opObj in ins.getOpObjects(0) + ins.getOpObjects(1):
            sv = str(opObj)
        # EDX fourcc immediates (printable ASCII 4CC)
        for k in (0, 1):
            for opObj in ins.getOpObjects(k):
                sv = str(opObj)
                if len(sv) == 8 and all(c in "0123456789abcdefABCDEF" for c in sv):
                    v = int(sv, 16)
                    bs = bytes((v >> 24) & 0xFF, "latin1") + bytes((v >> 16) & 0xFF, "latin1") + \
                        bytes((v >> 8) & 0xFF, "latin1") + bytes(v & 0xFF, "latin1")
                    if all(32 <= x <= 126 for x in bs):
                        print("          ; fourcc %s = %r" % (sv, bs.decode("latin1")))


dump(0x4409d0, 0x440af8)
dump(0x441800, 0x441a00)
dump(0x441140, 0x441168)
                    if 0x56000000 <= v <= 0x7A5A5A5A:
                        c = bytes((v >> 24) & 0xFF, "latin1") + bytes((v >> 16) & 0xFF, "latin1") + \
                            bytes((v >> 8) & 0xFF, "latin1") + bytes(v & 0xFF, "latin1")
                        if all(32 <= (x & 0xFF) <= 126 for x in bytes((v >> 24) & 0xFF, "latin1") +
                               bytes((v >> 16) & 0xFF, "latin1") + bytes((v >> 8) & 0xFF, "latin1") +
                               bytes(v & 0xFF, "latin1")):
                            print("          ; fourcc %s = %r" % (sv, c.decode("latin1")))
        addr = listing.getInstructionAfter(addr).getAddress() if listing.getInstructionAfter(addr) else addr.add(1)


dump(0x4409d0, 0x440af8)
dump(0x441800, 0x441a00)
dump(0x441140, 0x441168)
