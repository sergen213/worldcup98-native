#!/usr/bin/env /usr/bin/python3
"""Extract ASCII + UTF-16LE strings with file offsets. Usage: strings_off.py file > out"""
import sys, re

def ascii_strings(b, minlen=4):
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, b):
        yield m.start(), m.group().decode('latin1')

def utf16_strings(b, minlen=4):
    for m in re.finditer(rb'(?:[\x20-\x7e][\x00]){%d,}' % minlen, b):
        yield m.start(), m.group().decode('utf-16-le')

path = sys.argv[1]
b = open(path,'rb').read()
out = []
for off, s in ascii_strings(b):
    out.append(f"{off:#010x} A {s}")
for off, s in utf16_strings(b):
    out.append(f"{off:#010x} U {s}")
out.sort(key=lambda x: int(x.split()[0],16))
print('\n'.join(out))
