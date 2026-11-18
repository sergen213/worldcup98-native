prog=currentProgram
af=prog.getAddressFactory(); mem=prog.getMemory()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/igmenu_tables.txt','w')
# DGROUP tables region: VA 0x57c7d8 .. 0x57c91c (u16 count + stride-4 entries)
start=af.getAddress("0x57c7d8"); end=af.getAddress("0x57c91c")
n=end.offset-start.offset
b=bytearray(n)
mem.getBytes(start,b)
def u16(o): return b[o]|(b[o+1]<<8)
def u32(o): return b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(b[o+3]<<24)
# scan for u16 count candidates at each offset; dump structure per table
offs={'0x57c7d8':0,'0x57c802':0x2a,'0x57c824':0x4c,'0x57c832':0x5a,'0x57c860':0x88,'0x57c886':0xae,'0x57c898':0xc0,'0x57c8be':0xe6,'0x57c8e0':0x108,'0x57c902':0x12a}
for name,o in sorted(offs.items(), key=lambda x:x[1]):
    cnt=u16(o)
    out.write('%s count=%d\n'%(name,cnt))
    for i in range(cnt):
        e=o+2+i*4
        out.write('  item %2d: fc=%04x action=%d flag=%d\n'%(i,u16(e),b[e+2],b[e+3]))
# FC remap table region sanity: DAT_005774a4 stride 4, 5102 dwords
out.write('\n--- remap sample for every fc id seen ---\n')
rs=af.getAddress("0x5774a4"); rb=bytearray(5102*4); mem.getBytes(rs,rb)
def remap(fc): return rb[fc*4]|(rb[fc*4+1]<<8)|(rb[fc*4+2]<<16)|(rb[fc*4+3]<<24)
for name,o in sorted(offs.items(), key=lambda x:x[1]):
    cnt=u16(o)
    for i in range(cnt):
        e=o+2+i*4
        fc=u16(e)
        out.write('%s item %2d fc=%04x -> remap=%08x\n'%(name,i,fc,remap(fc)&0xffffffff))
out.close(); print('DONE')
