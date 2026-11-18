prog=currentProgram
af=prog.getAddressFactory(); listing=prog.getListing()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/menupainter.txt','w')
def disasm(addr,label,rev=0xa0):
    out.write('; ==== %s @%s ====\n'%(label,hex(addr)))
    a=af.getAddress(hex(addr))
    n=0
    while n<rev:
        u=listing.getCodeUnitAt(a)
        if u is None: out.write('%s: UNDEFINED\n'%(a,)); break
        out.write('%s: %s\n'%(a,u))
        a=u.getNextAddress(); n+=1
disasm(0x440300,'0x440300',0x60)
disasm(0x440540,'RESETAUX',0x60)
disasm(0x44005c,'DRAW?0x44005c',0x60)
out.close(); print('DONE')
