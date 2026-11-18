from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/igmenu_dec4.txt','w')
def dec(ep,label):
    a=af.getAddress(ep); fn=None
    for x in fm.getFunctions(a,True): fn=x; break
    if fn is None:
        out.write(';\n; ==== %s @%s NO FN ====\n'%(label,ep)); return
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    out.write(';\n; ==== %s %s @%s ====\n'%(label,fn.getName(),ep))
    out.write(cf.getC() if cf else 'FAIL\n')
for ep,l in [('0x43fc00','REDRAW'),('0x43fc00','REDRAW'),('0x495bc0','REPLAYAVAIL'),('0x4336f4','AVAIL9'),('0x43b140','AVAIL9B'),('0x4461b4','SCENE-SWITCH'),('0x48bdb4','SUB048bdb4'),('0x436b58','SUB0436b58'),('0x4cf530','SUB04cf530')]:
    dec(ep,l)
out.close(); print('DONE')
