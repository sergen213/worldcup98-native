prog=currentProgram
af=prog.getAddressFactory(); listing=prog.getListing(); mem=prog.getMemory()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/menupainter.txt','w')
lang=prog.getLanguage()
def disasm(addr,label,rev=0xa0):
    out.write('; ==== %s @%s ====\n'%(label,hex(addr)))
    a=af.getAddress(hex(addr))
    base=int(a); mem.getBytes(a,rev+80,ba:=bytearray(rev+80)) if True else None
    o=0
    while o<rev:
        try:
            u=lang.parseByteInstruction(af.getAddress(hex(base+o)),ba[o:o+15])
        except Exception as ex:
            out.write('%08x: PARSEFAIL %s\n'%(base+o,ex)); break
        if u is None: out.write('%08x: NONE\n'%(base+o)); break
        ln=u.getLength()
        out.write('%08x: %-28s %s\n'%(base+o,' '.join('%02x'%b for b in ba[o:o+ln]),u))
        o+=ln
disasm(0x440300,'0x440300',0x80)
disasm(0x440540,'RESETAUX',0x90)
disasm(0x44005c,'DRAW?0x44005c',0x70)
disasm(0x4401a4,'GMNC',0x20)
out.close(); print('DONE')
