prog=currentProgram
af=prog.getAddressFactory(); fm=prog.getFunctionManager()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/menupainter.txt','w')
listing=prog.getListing()
def disasm(addr,label,rev=0x90):
    out.write('; ==== %s @%s ====\n'%(label,hex(addr)))
    a=af.getAddress(hex(addr))
    try:
        it=listing.getDisassembly(a)
        n=0
        while it.hasNext() and n<rev:
            u=it.next(); n+=1
            out.write('%s  %-40s %s\n'%(u.getMinAddress(),u.getDefaultRepresentation(),u.getDefaultDisposition()))
    except Exception as e:
        out.write('DISFAIL %s\n'%e)
disasm(0x44005c,'DRAW-slot?0x44005c',0x70)
disasm(0x440300,'0x440300',0x90)
disasm(0x440540,'RESETAUX',0xa0)
out.close(); print('DONE')
