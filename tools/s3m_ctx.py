"""Dump context around the differing bytes (decoded original vs standard).

Usage: python s3m_ctx.py <decoded.s3m> <standard.s3m> <file_offset>
"""
import struct
import sys

a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
pos = int(sys.argv[3], 0)

for i in range(pos - 24, pos + 24):
    mark = " <<<" if a[i] != b[i] else ""
    print(f"{i:06X}: dec={a[i]:02X} std={b[i]:02X}{mark}")
