prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager(); mem=prog.getMemory()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/menupainter.txt','w')
def disasm(addr,label,rev=0x60):
    a=af.getAddress(hex(addr)); fn=None
    for x in fm.getFunctions(a,True): fn=x; break
    if fn is None: out.write(';%s NO FN\n'%label); return
    body=list(fn.getBody())
    s=body[0].getOffset(); n=min(rev, fn.getBody().end.offset-s)
    b=bytearray(n); mem.getBytes(s.addOffset(0),n,b)
    addr0=int(s)
    ad=af.getAddress(hex(addr0)); out.write('; ==== %s %s @%s ====\n'%(label,fn.getName(),hex(addr0)))
    o=0
    while o<n:
        mx=None
        try: mx=mnemonicAssembler.assemble(b[o:o+15],addr0+o)
        except Exception: mx=None
        if mx is None or mx.length()==0: out.write('%08x: %02x db\n'%(addr0+o,b[o])); o+=1; continue
        ln=mx.length()
        hexb=' '.join('%02x'%x for x in b[o:o+ln])
        out.write('%08x: %-38s %s %s\n'%(addr0+o,hexb,mx.getMnemonicString(),mx.getDefaultString()))
        o+=ln
# mnemonic assembler via Ghidra
from ghidra.program.model.lang import Language
lang=prog.getLanguage()
from ghidra.disasm.classfactory import ClassFactory
class NoneMnemonic: pass
try:
    mnemonicAssembler=lang.getMnemonicAssembler(None)
except Exception:
    out.write('ASMFAIL %s\n' % 'lang issue'); mnemonicAssembler=None
if mnemonicAssembler:
    disasm(0x440300,'MENUDRAW',0x90)
    disasm(0x440540,'RESETAUX',0xa0)
    disasm(0x4401a4+0x10,'GMNC-HDR',0x20)
out.close(); print('DONE')
