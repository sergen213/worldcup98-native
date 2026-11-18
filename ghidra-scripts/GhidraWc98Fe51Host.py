from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass33fe51/fe51_host.txt','w')
for va in (0x459ff0,0x45a0fc,0x45a110,0x459f7c,0x447220):
    a=af.getAddress(hex(va))
    fn=fm.getFunctionContaining(a)
    if fn:
        r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
        out.write(';\n; ==== CONTAINING FUN %s @ %s for %s ====\n'%(fn.getName(),fn.getEntryPoint(),a))
        out.write((cf.getC() if cf else 'FAIL')[:20000]+'\n')
    else:
        out.write('; no function containing %s\n'%a)
out.close(); print('done')
