from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
fm=prog.getFunctionManager(); dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/tmp/cdcheck_dec1.txt','w')
names=['GetVolumeInformationA','GetVolumeInformation','GetDriveTypeA','GetDriveType','GetDiskFreeSpaceA','GetDiskFreeSpaceExA','GetLogicalDrives','QueryDosDeviceA','GetFileAttributesA','GetFileAttributesExA','GetCurDirA','SetCurrentDirectoryA','GetFullPathNameA','SearchPathA','GetFileType','GetDeviceDriverPathA']
targets=[]
for s in prog.getSymbolTable().getSymbols(True):
    if any(n in s.getName() for n in names): targets.append(s)
out=open('/tmp/cdsyms.txt','w')
for s in prog.getSymbolTable().getSymbols(True):
    n=s.getName()
    if 'Volume' in n or 'Drive' in n: out.write(n+' @'+str(s.getAddress())+' ext='+str(s.isExternal() if hasattr(s,'isExternal') else '?')+'\n')
out.close()
seen=set()
for s in targets:
    a=s.getAddress()
    out.write(';\n; ==== SYMBOL %s @ %s ====\n'%(s.getName(),a))
    refit=prog.getReferenceManager().getReferencesTo(a)
    for r in refit:
        fr=r.getFromAddress()
        fn=fm.getFunctionAt(fr)
        if fn is None:
            for x in fm.getFunctions(fr,True): fn=x; break
        out.write('   ref from %s in %s\n'%(fr, fn.getName() if fn else '?'))
        if fn and fn.getEntryPoint().toString() not in seen:
            seen.add(fn.getEntryPoint().toString())
            r2=dc.decompileFunction(fn,120,mon); cf=r2.getDecompiledFunction()
            out.write(';\n; ==== CALLER %s @ %s ====\n'%(fn.getName(),fn.getEntryPoint()))
            out.write(cf.getC() if cf else 'FAIL\n')
out.close(); print('WROTE', len(seen))
