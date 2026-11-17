# GhidraWc98Ig2000: FC accessor (FUN_004c31d0) call-site id grammar. READ-ONLY.
# Byte-scan the execute block for E8 rel32 -> 0x4c31d0; report EAX source via the
# EXISTING analyzed instruction stream (listing.getInstructionBefore), no analysis
# mutations (pyghidra saves the shared project on exit).
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import jpype

program = currentProgram
af = program.getAddressFactory()
fm = program.getFunctionManager()
listing = program.getListing()
mem = program.getMemory()
decomp = DecompInterface(); decomp.openProgram(program)
mon = ConsoleTaskMonitor()
import sys; sys.stdout.reconfigure(line_buffering=True)

def A(v): return af.getAddress("0x%x" % v)

acc = 0x4C31D0

blk = None
for b in mem.getBlocks():
    if b.isInitialized() and b.isExecute():
        blk = b
        break
base, size = int(blk.getStart().getOffset()), int(blk.getSize())

arr = jpype.JArray(jpype.JByte)(size)
mem.getBytes(blk.getStart(), arr)
data = bytes(arr)

sites = []
i = data.find(b"\xE8")
while i >= 0:
    rel = int.from_bytes(data[i+1:i+5], "little", signed=True)
    if base + i + 5 + rel == acc:
        sites.append(base + i)
    i = data.find(b"\xE8", i + 1)
print("=== CALL 0x4c31d0 sites:", len(sites))

for s in sites:
    f = fm.getFunctionContaining(A(s))
    src = []
    it = listing.getInstructionBefore(A(s))
    steps = 0
    while it is not None and steps < 10:
        m = it.getMnemonicString().lower()
        ops = it.getOpObjects(0)
        if m == "mov" and ops and str(ops[0]).upper() == "EAX":
            src.append(str(it.getAddress().getOffset()) + " " + it.toString())
        if m == "push":
            src.append(str(it.getAddress().getOffset()) + " " + it.toString())
        if m in ("call", "jmp", "ret"):
            break
        it = listing.getInstructionBefore(it.getAddress())
        steps += 1
    print("SITE 0x%x fn=%s %s" % (s, f.getName() if f else None,
          hex(f.getEntryPoint().getOffset()) if f else ""))
print("=== FIELD-SITES (mov r32,[r+0x40] / imul 0x18 within 16B before CALL)")
for s in sites:
    win = data[max(0, s-16):s]
    marks = []
    for j in range(len(win) - 2):
        if win[j] in (0x8B, 0x89) and (win[j+1] & 0xC0) == 0x40 and win[j+2] == 0x40:
            marks.append(("ld0x40", win[j+1], j))
        if win[j] == 0x6B and win[j+1] == 0x18:
            marks.append(("imul18", win[j+1], j))
        if win[j] == 0x69 and win[j+1] == 0x18:
            marks.append(("imul18", win[j+1], j))
    if marks:
        f = fm.getFunctionContaining(A(s))
        print("SITE 0x%x fn=%s %s" % (s, f.getName() if f else None, marks))

# decompile the IG-region functions that contain accessor sites (existing functions only)
seen = set()
for s in sites:
    f = fm.getFunctionContaining(A(s))
    if f is None:
        continue
    ep = int(f.getEntryPoint().getOffset())
    if not (0x440000 <= ep < 0x447000) or ep in seen:
        continue
    seen.add(ep)
    df = decomp.decompileFunction(f, 120, mon).getDecompiledFunction()
    print("=== DECOMP IG %s\n%s" % (f.getName(), df.getC() if df else "FAIL"))
