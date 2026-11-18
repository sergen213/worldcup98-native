from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import re
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass34fe94/fe94_dec2.txt','w')
seen=set()
pending=[]
def callees(txt):
    return [int(m.group(1),16) for m in re.finditer(r'FUN_([0-9a-f]{8})', txt)
            if 0x400000<=int(m.group(1),16)<0x500000]
def decomp(ep):
    a=af.getAddress(hex(ep))
    fn=fm.getFunctionAt(a)
    if fn is None:
        cmd=CreateFunctionCmd(a); cmd.applyTo(prog)
        fn=fm.getFunctionAt(a)
    if fn is None:
        out.write(';\n; ==== NO FUNCTION @ %s ====\n'%a); return
    seen.add(ep)
    out.write(';\n; ==== FUN %s @ %s ====\n'%(fn.getName(),fn.getEntryPoint()))
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    txt=cf.getC() if cf else 'FAIL\n'
    out.write(txt)
    pending.extend(t for t in callees(txt) if t not in seen)
work=[0x46999c,0x469904,0x46986c]
for ep in work: decomp(ep)
lvl1=list(pending); pending=[]
for ep in lvl1:
    if ep not in seen: decomp(ep)
for ep in pending[:40]:
    if ep not in seen: decomp(ep)
out.close(); print('WROTE', len(seen), 'funcs')
