# Pass-44 HubA2: dump raw instruction listings of the hub wedge-dispatch region and the
# keyboard-payload producer, decoding 32-bit immediates as ASCII where printable.
from ghidra.program.model.address import AddressSet

program = currentProgram
addrf = program.getAddressFactory()
listing = program.getListing()


def toA(a):
    return addrf.getAddress("0x%08x" % a)


def ascii_of(v):
    b = bytes([(v >> (8 * i)) & 0xFF for i in range(4)])
    s = b.decode("latin-1")
    return s if all(0x20 <= ord(c) <= 0x7E for c in s) else None


for lo, hi in ((0x449BE0, 0x449E60), (0x445A00, 0x445C00)):
    print("### region 0x%08x-0x%08x" % (lo, hi))
    s = AddressSet(toA(lo), toA(hi))
    for ins in listing.getInstructions(s, True):
        extra = ""
        for i in range(ins.getNumOperands()):
            for obj in ins.getOpObjects(i):
                try:
                    v = int(obj.getUnsignedValue())
                except Exception:
                    try:
                        v = int(obj)
                    except Exception:
                        continue
                if 0x400000 <= v <= 0x60000000:
                    a = ascii_of(v)
                    extra += "  [0x%x%s]" % (v, (" '%s'" % a) if a else "")
        print("%s  %s%s" % (ins.getMinAddress(), ins, extra))
