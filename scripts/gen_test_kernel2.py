#!/usr/bin/env python3
"""Minimal ARM64 kernel — prints to PL011 UART, no FIFO check."""
import struct, sys

code = bytearray()

# movz x0, #0x1000, lsl #16  →  x0 = 0x10000000 (UART base)
code += struct.pack('<I', 0xD2A20000)

# adr x1, string (PC=0x04, target=0x20, offset=0x1C)
# immlo = (0x1C & 3) << 29 = 0
# immhi = (0x1C >> 2) << 5 = 7 << 5 = 0xE0
# insn = 0x10000000 | 0xE0 | 1 = 0x100000E1
code += struct.pack('<I', 0x100000E1)

# loop (offset 0x08):
# ldrb w8, [x1], #1
code += struct.pack('<I', 0x38401428)

# cbz w8, done (offset from 0x0C to 0x14 = +2 insns)
code += struct.pack('<I', 0x34000408)

# strb w8, [x0]  — write to UART DR
code += struct.pack('<I', 0x39000008)

# b loop (from 0x14 to 0x08, -3 insns)
code += struct.pack('<I', 0x17FFFFFD)

# done (offset 0x18):
# wfi
code += struct.pack('<I', 0xD503207F)
# b done (offset -1)
code += struct.pack('<I', 0x17FFFFFE)

# padding to 0x20
code += b'\x00' * (0x20 - len(code))

# string at offset 0x20
code += b"Hello SiliconV!\n\0"

while len(code) < 512:
    code += b'\x00'

with open(sys.argv[1], 'wb') as f:
    f.write(code)
print(f"OK: {len(code)} bytes")
