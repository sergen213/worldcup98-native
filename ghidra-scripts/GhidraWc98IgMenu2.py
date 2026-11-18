from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/igmenu_dec2.txt','w')
def dec(ep, label):
    a=af.getAddress(ep); fn=None
    for x in fm.getFunctions(a,True): fn=x; break
    if fn is None:
        out.write(';\n; ==== %s @%s: NO FUNCTION ====\n'%(label,ep)); return
    r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
    out.write(';\n; ==== %s %s @%s ====\n'%(label,fn.getName(),ep))
    out.write(cf.getC() if cf else 'FAIL\n')
def xrefs(addr,label):
    a=af.getAddress(addr)
    refs=list(prog.getReferenceManager().getReferencesTo(a))
    out.write(';\n; ==== XREFS %s %s refs=%d ====\n'%(label,addr,len(refs)))
    seen=set()
    for ref in refs:
        fr=ref.getFromAddress()
        fn=None
        for x in fm.getFunctions(fr,True): fn=x; break
        fn=fn or fm.getFunctionAt(fr)
        key=fn.getEntryPoint().toString() if fn else str(fr)
        out.write('   from %s in %s\n'%(fr, fn.getName() if fn else '?'))
        if fn and key not in seen:
            seen.add(key)
            r=dc.decompileFunction(fn,120,mon); cf=r.getDecompiledFunction()
            out.write(';\n; ==== REF FUN %s @%s ====\n'%(fn.getName(),fn.getEntryPoint()))
            out.write(cf.getC() if cf else 'FAIL\n')
dec('0x43fc40','TICK')
dec('0x43ff8c','DRAW')
dec('0x43fbe0','CALLER-of-401a4')
dec('0x439d38','RESUME')
dec('0x43a3f4','CLOSEMENU')
dec('0x43ab9c','SUB043ab9c')
dec('0x446154','SUB046154')
dec('0x440750','SUB0440750')
dec('0x440540','SUB0440540')
xrefs('0x6024d0','DAT_006024d0-items')
out.close(); print('DONE')
