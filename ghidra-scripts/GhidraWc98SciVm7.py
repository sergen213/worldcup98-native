# SCI VM pass 7: xrefs to .SCI relocation target VAs (arena blocks + trailer targets).
from ghidra.util.task import ConsoleTaskMonitor
prog = currentProgram
af = prog.getAddressFactory()
fm = prog.getFunctionManager()
refmgr = prog.getReferenceManager()
def gaddr(a):
    return af.getAddress("0x%x" % a)

targets = [0x582054, 0x58c054, 0x58d054, 0x5a1054, 0x5a30cc, 0x5a3150, 0x5a2054]
out = []
for da in targets:
    out.append("=== XREFS TO %s ===" % hex(da))
    for r in refmgr.getReferencesTo(gaddr(da)):
        fr = r.getFromAddress()
        f = fm.getFunctionContaining(fr)
        out.append("  from %s (%s) type %s" % (fr, f.getName() if f else None, r.getReferenceType()))

# also scan a stride-0x1000 sweep for any xrefs in [0x582054..0x5a20ff] band heads
hits = {}
for da in range(0x582000, 0x5a3200, 4):
    rs = list(refmgr.getReferencesTo(gaddr(da)))
    if rs:
        for r in rs:
            fr = int(str(r.getFromAddress()), 16)
            f = fm.getFunctionContaining(r.getFromAddress())
            key = (f.getName() if f else str(fr))
            hits.setdefault(key, []).append((hex(da), str(r.getFromAddress())))
out.append("=== BAND SCAN SUMMARY ===")
for k, v in sorted(hits.items()):
    out.append("%s: %d refs %r" % (k, len(v), v[:6]))
open("analysis/pass17sci/DECOMPILE/scivm7.txt", "w").write("\n".join(out))
print("WROTE scivm7")
