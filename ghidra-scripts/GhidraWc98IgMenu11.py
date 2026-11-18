prog=currentProgram
af=prog.getAddressFactory()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/menupainter.txt','w')
listing=prog.getListing()
def disasm(addr,label,rev=0x90):
    out.write('; ==== %s @%s ====\n'%(label,hex(addr)))
    a=af.getAddress(hex(addr))
    try:
        it=listing.getDisassemblerAtAddress(a)
        if it is None: out.write('no disasm\n'); return
        n=0
        while n<rev:
            it.disassemble(a)
            u=it.getInstruction(a)
            if u is None: break
            out.write('%s  %s   %s\n'%(a,u.getBytes().toString().replace(' ',''),u))
            a=u.getNextAddress(); n+=1
    except Exception as e:
        out.write('DISFAIL %s\n'%e)
disasm(0x44005c,'DRAW?0x44005c',0x70)
disasm(0x440300,'0x440300',0xa0)
disasm(0x440540,'RESETAUX',0xa0)
out.close(); print('DONE')
