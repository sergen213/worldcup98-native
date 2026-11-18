prog=currentProgram
af=prog.getAddressFactory(); mem=prog.getMemory()
out=open('/mnt/1st4TB/WorldCup98/analysis/pass25igmenu/paintertable.txt','w')
# scan Ghidra VA data region for a dword table containing 0x004869c8
start=af.getAddress("0x570000"); end=af.getAddress("0x680000")
n=end.offset-start.offset; b=bytearray(n); mem.getBytes(start,b)
import struct
target=struct.pack('<I',0x4869c8)
occ=[]
o=0
while True:
    i=b.find(target,o)
    if i<0: break
    occ.append(i); o=i+1
print(out.write('hits %s'%[hex(start.offset+x) for x in occ[:10]]))
out.close(); print('OK',len(occ))
