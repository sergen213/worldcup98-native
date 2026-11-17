# GhidraWc98Lang19: scan code for imm fourcc 'rows'/'help'/'text'/'list' sites + containing fn.
import struct
mem = currentProgram.getMemory()
af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()

targets = {0x77736F72: "rows", 0x706C6568: "help", 0x74636574: "text", 0x7473696C: "list"}

# executable segments
segs = []
for s in mem.getMemoryBlocks():
    if s.isExecute():
        segs.append((s.getStart().getValue(), s.getEnd().getValue() + 1))
print("exec segments: %s" % ["0x%x-0x%x" % r for r in segs])
# chunked read
found = {}
for t, name in targets.items():
    needle = struct.pack("<I", t)
    cnt = 0
    for lo, hi in segs:
        pos = lo
        while pos < hi:
            n = min(1 << 20, hi - pos)
            chunk = bytearray(n)
            a = af.getAddress("0x%08x" % pos)
            mem.getBytes(a, n, chunk, 0)
            i = 0
            while True:
                j = chunk.find(needle, i)
                if j < 0:
                    break
                site = pos + j
                found.setdefault(name, []).append(site)
                cnt += 1
                i = j + 1
            pos += n
    print("scan %s: %d sites" % (name, cnt))

for name, sites in sorted(found.items()):
    print("=== %s ===" % name)
    for site in sites:
        a = af.getAddress("0x%08x" % site)
        fn = fm.getFunctionContaining(a)
        fns = ("0x%x" % fn.getEntryPoint().getValue()) if fn else "?"
        # instruction containing this imm: walk back
        ia = a
        for _ in range(6):
            ins = listing.getInstructionAt(ia)
            if ins is not None:
                break
            prev = listing.getInstructionBefore(ia)
            if prev is None:
                break
            ia = prev.getAddress()
        ins = listing.getInstructionAt(ia)
        print("  site=0x%x fn=%s ins=%s" % (site, fns, str(ins) if ins else "?"))
