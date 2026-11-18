from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass33fe51/fe51_dec1.txt','w')
eps=[0x459ff0,0x45a0fc,0x45a110]
seen=set()
def decomp(ep):
    a=af.getAddress(hex(ep))
    fn=fm.getFunctionAt(a)
    if fn is None:
        out.write(';\n; ==== NO FUNCTION @ %s ====\n'%a); return []
    out.write(';\n; ==== FUN %s @ %s ====\n'%(fn.getName(),fn.getEntryPoint()))
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    out.write(cf.getC() if cf else 'FAIL\n')
    callees=[]
    for e in fm.getFunctionCallingReferences(fn):
        cf2=e.getFunction()
        callees.append(cf2.getEntryPoint().getSegmentOffset()+0x400000)
    return callees
for ep in eps:
    seen.add(ep); decomp(ep)
for ep in eps:
    a=af.getAddress(hex(ep)); fn=fm.getFunctionAt(a)
    if fn is None: continue
    for e in fm.getFunctionCallingReferences(fn):
        t=e.getFunction().getEntryPoint().getSegmentOffset()+0x400000
        if t not in seen and 0x400000<=t<0x500000:
            seen.add(t); decomp(t)
out.close(); print('WROTE', len(seen), 'funcs')
