import jpype
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/igmenu_dec3.txt','w')
for ep in ['0x43fc40','0x43ff8c','0x4401a4']:
    a=af.getAddress(ep)
    cmd=CreateFunctionCmd(a)
    ok=cmd.applyTo(prog)
    out.write('create %s ok=%s\n'%(ep,ok))
dc.openProgram(prog)
def dec(ep,label):
    a=af.getAddress(ep); fn=fm.getFunctionAt(a)
    if fn is None:
        out.write(';\n; ==== %s @%s NO FN ====\n'%(label,ep)); return
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    out.write(';\n; ==== %s %s @%s ====\n'%(label,fn.getName(),ep))
    out.write(cf.getC() if cf else 'FAIL\n')
dec('0x43fc40','TICK')
dec('0x43ff8c','DRAW')
dec('0x4401a4','INIT-GMNC')
# linear disasm 0x43fc40..0x440540
listing=prog.getListing()
start=af.getAddress('0x43fc40'); end=af.getAddress('0x440540')
addr=start
out.write(';\n; ==== LINEAR DISASM 0x43fc40..0x440540 ====\n')
while addr and addr.compareTo(end)<0:
    ins=listing.getInstruction(addr,mon)
    if ins is None:
        addr=addr.next(); continue
    out.write('%s: %s\n'%(addr,ins.toString()))
    addr=ins.getAddress().next()
out.close(); print('DONE')
