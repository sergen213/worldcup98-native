from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/painters.txt','w')
def dec(addr,label):
    a=af.getAddress(hex(addr)); fn=None
    for x in fm.getFunctions(a,True): fn=x; break
    if fn is None:
        out.write(';\n; ==== %s @%s NO FN ====\n'%(label,hex(addr))); return
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    out.write(';\n; ==== %s %s @%s ====\n'%(label,fn.getName(),hex(addr)))
    out.write(cf.getC() if cf else 'FAIL\n')
for ep,l in [('0x486a16','ROWCAP'),('0x486b15','ROWCAPB'),('0x4a6b30','SETHILITE'),('0x47a8b4','STRFALLBACK'),('0x440540','RESETAUX'),('0x440750','PREOPEN'),('0x48a5e0','BOXCHILD')]:
    dec(int(ep,16),l)
out.close(); print('DONE')
