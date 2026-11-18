from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/painters.txt','w')
def dec(addr,off,label):
    a=af.getAddress(hex(addr+off)); fn=None
    for x in fm.getFunctions(a,True): fn=x; break
    if fn is None:
        out.write(';\n; ==== %s @%s NO FN ====\n'%(label,hex(addr+off))); return
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    out.write(';\n; ==== %s idx%d %s @%s ====\n'%(label,off//4,fn.getName(),hex(addr+off)))
    out.write(cf.getC() if cf else 'FAIL\n')
ptr=0x0048a414
for idx in (0,3,4,6,8,9,10):
    dec(ptr,idx*4,'PAINTER')
out.close(); print('DONE')
