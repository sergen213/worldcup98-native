# PyGhidra pass 4: locate the real resolveCollision entry near 0x4c0997.
prog = currentProgram
fm = prog.getFunctionManager()
la = prog.getListing()
addrf = prog.getAddressFactory()


def ga(v):
    return addrf.getAddress("0x%08x" % v)


def av(a):
    return int(str(a), 16)


it = fm.getFunctions(True)
while it.hasNext():
    f = it.next()
    s = av(f.getBody().getMinAddress())
    if s >= 0x4C1200:
        break
    if s >= 0x4C0800:
        print("FUNC", f.getName(), hex(s), "end", hex(av(f.getBody().getMaxAddress())))

a = ga(0x4C0997)
for i in range(12):
    ins = la.getInstructionAt(a)
    if ins is None:
        print(hex(av(a)), "no instr")
        break
    print(hex(av(ins.getAddress())), str(ins))
    a = ins.getNextAddress()
