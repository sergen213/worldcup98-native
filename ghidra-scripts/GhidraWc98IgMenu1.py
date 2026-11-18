from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
dc=DecompInterface(); dc.openProgram(prog); mon=ConsoleTaskMonitor()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/igmenu_dec1.txt','w')
names={0x163a75:'Team Management',0x163a88:'User Controlled',0x163a98:'AI Assisted',
0x163e41:'Post Game',0x163e4d:'Continue',0x163e59:'Instant Replay',0x163e69:'Match Stats',
0x163e79:'Score Summary',0x163e89:'Quit Match',0x163e95:'Quit World Cup',0x163ea5:'Restart Match',
0x163eb5:'End Fifa WC98',0x1641a1:'Next Classic',0x1641b1:'Previous Classic',0x1641c5:'Next Trivia',
0x1641d5:'Previous Trivia',0x1641e9:"Quit FIFA World Cup '98",0x164279:'Foul Summary',
0x164289:'Resume Match',0x164299:'Extra Time',0x1642a5:'Go To Shooutout',0x1642b9:'End Match',
0x1642c5:'Resume Practice',0x1642d9:'Restart Practice',0x1642ed:'Restart Scenario',0x164301:'Quit Practice',
0x1644b9:'Resume Shooutout',0x1644cd:'Restart Shooutout',0x1644e1:'Quit Shooutout',0x1644f1:'Replay Shooutout',
0x163ec5:'Defence',0x163ed0:'Midfield',0x163edc:'Forwards',0x163ee9:'Attacking Bias',0x163ef8:'Strategy',
0x163f05:'Player Aggression Level',0x163f21:'Player Position',0x163f35:'Trivia'}
callers={}
for r,n in sorted(names.items()):
    a=af.getAddress(hex(r+0x401a00))
    refs=list(prog.getReferenceManager().getReferencesTo(a))
    out.write(';\n; ==== STRING VA %s "%s" refs=%d ====\n'%(a,n,len(refs)))
    for ref in refs:
        fr=ref.getFromAddress()
        fn=None
        for x in fm.getFunctions(fr,True): fn=x; break
        fn=fn or fm.getFunctionAt(fr)
        out.write('   ref from %s in %s\n'%(fr, fn.getName() if fn else '?'))
        if fn: callers.setdefault(fn.getEntryPoint().toString(), fn)
for ep,fn in sorted(callers.items()):
    r2=dc.decompileFunction(fn,120,mon); cf=r2.getDecompiledFunction()
    out.write(';\n; ==== FUN %s @ %s ====\n'%(fn.getName(),fn.getEntryPoint()))
    out.write(cf.getC() if cf else 'FAIL\n')
out.close(); print('WROTE', len(callers), 'callers')
