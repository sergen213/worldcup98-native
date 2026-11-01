#!/usr/bin/env /usr/bin/python3
"""Clean raw PE parser for Watcom 2.x PE files: sections, imports, exports, resources(+VERSION)."""
import sys, struct, math, os, datetime
from collections import Counter

def ent(b):
    if not b: return 0.0
    c = Counter(b); n = len(b)
    return -sum((v/n)*math.log2(v/n) for v in c.values())

DIRS = ['EXPORT','IMPORT','RESOURCE','EXCEPTION','SECURITY','BASERELOC','DEBUG','ARCH','TLS','LOADCFG','BOUND','IAT','DELAY','CLR']

def parse(path):
    b = open(path,'rb').read()
    fsize = len(b)
    e_lfanew = struct.unpack_from('<I', b, 0x3c)[0]
    mach, nsec = struct.unpack_from('<HH', b, e_lfanew+4)
    tds, symptr, nsyms = struct.unpack_from('<III', b, e_lfanew+8)
    szopts, fchars = struct.unpack_from('<HH', b, e_lfanew+20)
    opt = e_lfanew + 24
    magic = struct.unpack_from('<H', b, opt)[0]
    lmaj, lmin = b[opt+2], b[opt+3]
    codesize, initdata, uninit, entry, basecode, baseofdata = struct.unpack_from('<IIIIII', b, opt+4)
    imagebase, sectalign, filalign, sizeimg, sizehdr = struct.unpack_from('<IIIII', b, opt+28)
    subsys = struct.unpack_from('<H', b, opt+56)[0]
    print("="*100)
    print(f"FILE: {path} size={fsize}")
    try: tdt = datetime.datetime.fromtimestamp(tds, datetime.UTC).strftime('%Y-%m-%d %H:%M:%S UTC')
    except Exception: tdt = '?'
    print(f"Machine=0x{mach:04X} nsec={nsec} Timestamp={tds} ({tdt if 0x20000000<tds<0x90000000 else 'n/a'}) "
          f"symtab@{symptr} nsyms={nsyms} SizeOfOptional={szopts} FileChars=0x{fchars:04X}")
    print(f"magic=0x{magic:03X} linker={lmaj}.{lmin} EntryRVA={entry:#x} ImageBase={imagebase:#x} "
          f"SizeOfImage={sizeimg:#x} SizeOfHeaders={sizehdr:#x} Subsystem={subsys} SectAlign={sectalign} FileAlign={filalign}")
    print(f"BaseOfCode={basecode:#x} BaseOfData={baseofdata:#x} SizeOfCode={codesize:#x} SizeOfInitData={initdata:#x} SizeOfUninitData={uninit:#x}")
    sections = []
    maxraw = 0
    sect_hdr = opt + szopts
    for i in range(nsec):
        so = sect_hdr + i*40
        name = b[so:so+8].rstrip(b'\x00').decode('latin1')
        vsize, va, rawsize, rawoff = struct.unpack_from('<IIII', b, so+8)
        flags = struct.unpack_from('<I', b, so+36)[0]
        data = b[rawoff:rawoff+rawsize] if rawsize and rawoff else b''
        e = ent(data)
        maxraw = max(maxraw, rawoff + rawsize)
        sections.append((name, va, vsize, rawoff, rawsize, flags))
        print(f"  sec[{i}] {name:<9} VA={va:#08x} VS={vsize:#08x} RO={rawoff:#07x} RS={rawsize:#07x} ent={e:.2f} flags=0x{flags:08X}")
    print(f"  overlay: file({fsize}) - maxRawEnd({maxraw}) = {fsize-maxraw}")
    n_ddir = max(0, (szopts - 96)//8)
    d = {}
    for i in range(min(n_ddir, 16)):
        rva, sz = struct.unpack_from('<II', b, opt + 96 + i*8)
        if rva:
            nm = DIRS[i] if i < len(DIRS) else str(i)
            d[nm] = (rva, sz)
            print(f"  ddir[{i}] {nm}: RVA={rva:#x} size={sz}")
    def r2o(rva):
        for i, (name, va, vsize, rawoff, rawsize, flags) in enumerate(sections):
            if flags & 0x80:  # .bss / uninitialized — no file data
                continue
            nxt = sections[i+1][1] if i+1 < len(sections) else sizeimg
            span = max(vsize, rawsize, nxt - va if nxt > va else 0)
            if va and va <= rva < va + span:
                return rawoff + (rva - va)
        return None
    print("IMPORTS:")
    if 'IMPORT' in d:
        off = r2o(d['IMPORT'][0]); tot = 0
        while off and off+20 <= fsize:
            ilt, tstamp, forward, name_rva, iat = struct.unpack_from('<IIIII', b, off)
            if name_rva == 0: break
            noff = r2o(name_rva)
            dll = b[noff:b.index(b'\x00', noff)].decode('latin1')
            ilt_rva = ilt if ilt else iat
            ioff = r2o(ilt_rva)
            fn = []
            while ioff:
                q = struct.unpack_from('<I', b, ioff)[0]
                if q == 0: break
                if q & 0x80000000:
                    fn.append(f"#{q & 0xFFFF}")
                else:
                    no = r2o(q)
                    fn.append(b[no+2:b.index(b'\x00', no+2)].decode('latin1'))
                ioff += 4
            print(f"  {dll} ({len(fn)})")
            for x in fn: print(f"      {x}")
            tot += len(fn)
            off += 20
        print(f"  TOTAL IMPORTS: {tot}")
    print("EXPORTS:")
    if 'EXPORT' in d:
        off = r2o(d['EXPORT'][0])
        flags, tstamp, mj, mn, nameRVA, ordbase, nmots, nsyms, addr_off, name_ptr_off, ord_map_off = struct.unpack_from('<IIHHIIIIIII', b, off)
        noff = r2o(nameRVA); dlln = b[noff:b.index(b'\x00', noff)].decode('latin1')
        aoff, npoff, ooff = r2o(addr_off), r2o(name_ptr_off), r2o(ord_map_off)
        exp_start, exp_end = d['EXPORT'][0], d['EXPORT'][0]+d['EXPORT'][1]
        rows = []
        for i in range(nsyms):
            nptr = struct.unpack_from('<I', b, npoff + i*4)[0]
            o = r2o(nptr); nm = b[o:b.index(b'\x00', o+2) if False else o].decode('latin1') if nptr else f"ord{ordbase+i}"
            if nptr:
                nm = b[o:b.index(b'\x00', o)].decode('latin1')
            ordinal = struct.unpack_from('<H', b, ooff + i*2)[0]
            rva = struct.unpack_from('<I', b, aoff + ordinal*4)[0]
            fwd = ''
            if rva and exp_start <= rva < exp_end:
                fo = r2o(rva); fwd = " -> " + b[fo:b.index(b'\x00', fo)].decode('latin1')
            rows.append((nm, ordbase+ordinal, rva, fwd))
        print(f"  DLL={dlln} ordinal_base={ordbase} #names={nsyms} #functions={nmots}")
        for nm, o, rva, fwd in sorted(rows):
            print(f"      ord {o}: {nm} RVA={rva:#x}{fwd}")
    if 'RESOURCE' in d:
        roff0 = r2o(d['RESOURCE'][0])
        RT = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',8:'FONTDIR',9:'FONT',10:'ACCEL',12:'GROUP_CURSOR',14:'GROUP_ICON',16:'VERSION',24:'MANIFEST'}
        print(f"RESOURCES (tree@{roff0:#x}):")
        def node(o):
            n_named, n_id = struct.unpack_from('<HH', b, o+12)
            kids = []
            for i in range(n_named + n_id):
                d1, d2 = struct.unpack_from('<II', b, o+16+i*8)
                sub = roff0 + (d2 & 0x7fffffff)
                if d1 & 0x80000000:
                    so = roff0 + (d1 & 0x7fffffff)
                    ln = struct.unpack_from('<H', b, so)[0]
                    nm = b[so+2:so+2+ln*2].decode('utf-16-le', 'replace')
                else:
                    nm = d1
                kids.append((nm, sub))
            return kids
        for typ, tsub in node(roff0):
            tname = RT.get(typ, str(typ)) if isinstance(typ, int) else typ
            for nm, isub in node(tsub):
                for lang_ent, lsub in node(isub):
                    drva, dsize, res_rva, res_size = struct.unpack_from('<IIII', b, lsub)
                    codepage = struct.unpack_from('<I', b, lsub+12)[0]
                    print(f"  type={tname} name={nm} langid=0x{codepage:04x} data_rva={res_rva:#x} size={res_size}")
                    if tname == 'VERSION':
                        doff = r2o(res_rva)
                        if doff is None:
                            print(f"    (VERSION data RVA {res_rva:#x} not mapped to file — skipping content dump)")
                            continue
                        data = b[doff:doff+res_size]
                        j = next((k for k in range(0, len(data)-52, 4) if struct.unpack_from('<I', data, k)[0] == 0xFEEF04BD), None)
                        if j is not None:
                            f = struct.unpack_from('<10I', data, j)
                            print(f"    FILEVERSION={f[2]>>16}.{(f[2]>>8)&0xFF}.{f[2]&0xFF}.{f[3]>>16}{'' if False else ''} "
                                  f"prod={f[4]>>16}.{f[4]&0xFFFF}.{f[5]>>16}.{f[5]&0xFFFF} "
                                  f"file={f[2]>>16}.{f[2]&0xFFFF}.{f[3]>>16}.{f[3]&0xFFFF} type={f[9]} date={(f[6] | (f[7]<<32))}")
                        # string tables: brute-force utf16 keys
                        i = 0
                        seen = set()
                        while True:
                            si = data.find('StringFileInfo'.encode('utf-16-le'), i)
                            if si < 0: break
                            seg = data[si:si+2000]
                            # keys are latin1 encoded as ascii
                            kpos = 0
                            while True:
                                kp = seg.find(b'\x00'.join(bytes(c, 'ascii') for c in 'AAAA'), kpos)  # unused
                                break
                            # parse sequentially: ascii keys followed by utf16 values
                            import re
                            for m in re.finditer(rb'([\x20-\x7e]{3,31})\x00', seg):
                                k = m.group(1).decode('ascii')
                                vo = (m.end() + 3) & ~3
                                v = seg[vo:vo+200].split(b'\x00\x00')[0]
                                try: vv = v.decode('utf-16-le')
                                except Exception: vv = v.hex()
                                if len(vv) < 200 and k not in seen and not k.startswith('0000'):
                                    seen.add(k)
                                    if k in ('040904b0','Translation') or len(k) < 32:
                                        print(f"    {k} = {vv!r}")
                            i = si + 2
    print()

for p in sys.argv[1:]:
    try: parse(p)
    except Exception:
        import traceback; print("FILE:", p); traceback.print_exc()
