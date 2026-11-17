# Pass-21 IgScn1: in-game scene registry (function containing 0x47872d) linear scan
# -> SCENE name/handler-slot map; then decompile igoption/igcntrol handler slots.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

program = currentProgram
fm = program.getFunctionManager()
mem = program.getMemory()
addrf = program.getAddressFactory()
listing = program.getListing()


def toA(a):
    return addrf.getAddress("0x%08x" % a)


def readStr(a, maxlen=32):
    try:
        ln = mem.getByte(a) & 0xFF
        if 3 < ln <= maxlen + 1:
            buf = bytearray(ln)
            mem.getBytes(a.add(1), buf)
            s = bytes(buf).decode("ascii", "replace")
            if all(32 <= ord(c) < 127 for c in s):
                return s
    except Exception:
        pass
    return None


dec = DecompInterface()
dec.openProgram(program)
mon = ConsoleTaskMonitor()


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


regf = fm.getFunctionContaining(toA(0x47872D))
print("=== REGISTRY func containing 0x47872d: %s" % (regf.getName() if regf else "MISSING"))
if regf:
    body = regf.getBody()
    start, end = body.getMinAddress(), body.getMaxAddress()
    print("   body %s .. %s" % (start, end))
    entries = []
    ins = listing.getInstructionAt(start)
    while ins is not None and ins.getAddress().compareTo(end) <= 0:
        m = ins.toString()
        if m.startswith("MOV EAX,0x564"):
            t = m.split(",", 1)[1].strip().rstrip("hH")
            try:
                v = int(t, 16) & 0xFFFFFFFF
            except Exception:
                v = None
            if v is not None:
                s = readStr(toA(v))
                entries.append([ins.getAddress().toString(), hex(v), s, {}])
        elif m.startswith("MOV dword ptr [EAX + ") and entries:
            try:
                off = int(m.split("[EAX + ")[1].split("]")[0], 16)
                t = m.split(",", 1)[1].strip().rstrip("hH")
                try:
                    val = int(t, 16) & 0xFFFFFFFF
                except Exception:
                    val = None
                if val is not None:
                    entries[-1][3]["+0x%x" % off] = hex(val)
            except Exception:
                pass
        ins = listing.getInstructionAfter(ins.getAddress())
    print("--- SCENEMAP IG (%d entries)" % len(entries))
    for addr, ptr, name, slots in entries:
        s = " ".join("%s=%s" % (k, v) for k, v in sorted(slots.items()))
        print("SCENE site=%s %s %s %s" % (addr, ptr, name, s))

    # decompile handlers for the igoption + igcntrol scenes
    targets = [e for e in entries if e[2] in ("igoption", "igcntrol")]
    for addr, ptr, name, slots in targets:
        for k, v in sorted(slots.items()):
            va = int(v, 16)
            f = fm.getFunctionContaining(toA(va))
            print("=== %s %s -> 0x%x (%s)" % (name, k, va, f.getName() if f else "MISSING"))
            if f:
                txt = deco(f)
                if txt:
                    for line in txt.splitlines():
                        print("   | " + line)
