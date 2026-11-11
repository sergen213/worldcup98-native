# PyGhidra pass 5: dump the 0x4c0975..0x4c09a0 gap listing.
prog = currentProgram
la = prog.getListing()


def ga(v):
    return prog.getAddressFactory().getAddress("0x%08x" % v)


def av(a):
    return int(str(a), 16)


a = ga(0x4C0975)
while av(a) < 0x4C09A0:
    ins = la.getInstructionAt(a)
    if ins is None:
        print(hex(av(a)), "UNDISJ")
        break
    print(hex(av(ins.getAddress())), str(ins))
    a = ins.getNextAddress()
