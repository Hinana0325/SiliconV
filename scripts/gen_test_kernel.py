#!/usr/bin/env python3
"""
Generate a minimal ARM64 bare-metal kernel for SiliconV testing.
Prints "Hello SiliconV!\n" to PL011 UART at 0x10000000, then halts.
"""

import struct, sys

UART_BASE = 0x10000000  # PL011 physical address
TEXT = b"Hello SiliconV!\n\0"

# ARM64 instructions (little-endian)
def insn(words):
    return b''.join(struct.pack('<I', w) for w in words)

code = bytearray()

# === Prologue ===
# Save x0 (DTB addr) and x30 (LR)
# We won't touch x0 so DTB stays valid

# === Load UART base into x0 ===
# movz x0, #0x1000, lsl #16  →  x0 = 0x10000000
code += insn([0xD2A20000])

# === Load string address into x1 ===
# The string is at offset 0x38 from the start of this code
# (8 instructions * 4 bytes = 0x20 for code, but let's compute)
# Code instructions: 8 instructions = 32 bytes (0x20)
# String starts at offset 0x20 from code start
# PC at instruction 2 = code_base + 8
# ADRP x1, #0  → loads page address of current PC
# We'll use a simpler approach: load from a known offset

# Actually, let's use a data pointer approach.
# We'll write the string address using MOVZ+MOVK
# The string will be at code_base + 0x20 (after 8 instructions)
# But we don't know code_base at compile time...
# Solution: use ADR to load PC-relative address

# adr x1, .+0x20  (string is 0x20 bytes ahead from this instruction)
# ADR: 0x10000000 | (imm21 << 5) | Rt
# imm21 = 0x20 >> 2 = 8  (word-aligned offset)
# Actually ADR uses byte offset in imm21 encoding
# ADR Xd, label:  imm = (label - PC) encoded as immhi:immlo
# offset = 0x20 (32 bytes forward from current PC)
# immlo = (offset & 3) << 29 = 0
# immhi = (offset >> 2) << 5 = 8 << 5 = 0x100
# insn = 0x10000000 | immhi | immlo | 1  = 0x10000101
code += insn([0x10000101])  # adr x1, #0x20 (string at code+0x20)

# === Print loop ===
# loop: (offset 0x08)
# ldrb w8, [x1], #1  — load byte, post-increment
code += insn([0x38401428])

# cbz w8, halt  — if null byte, exit loop (offset +4 → halt at 0x14)
code += insn([0x34000088])

# Check TX FIFO not full
# ldrb w9, [x0, #0x18]  — read FR register (offset 0x18)
# LDRB W9, [X0, #0x18]: imm12 = 0x18
code += insn([0x39406009])

# tst w9, #0x20  — test bit 5 (TX FIFO full)
# TST W9, #0x20: ANDS WZR, W9, #0x20
code += insn([0x7200413F])

# b.ne loop  — if TX full, retry (branch back 4 instructions = -16 bytes)
# offset from here to loop: -16 bytes = -4 instructions
code += insn([0x54FFFFA1])  # b.ne, offset = -3 (back 4 insns from next)

# strb w8, [x0]  — write byte to UART DR (offset 0)
code += insn([0x39000008])

# b loop  — unconditional branch back to loop
# offset from here to loop: -24 bytes = -6 instructions
code += insn([0x17FFFFFA])  # b, offset = -6

# === halt: (offset 0x20) ===
# wfi — wait for interrupt
code += insn([0xD503207F])
# b halt  — infinite loop
code += insn([0x17FFFFFE])  # b, offset = -1

# === String data (offset 0x28 from code start) ===
# Wait, I calculated 0x20 for string but it's actually at 0x28 (after 10 instructions)
# Let me recount: 10 instructions * 4 = 40 = 0x28
# But ADR pointed to 0x20... let me fix
# Actually the ADR was at instruction index 1, PC = code_base + 4
# adr x1, #0x20 means x1 = (code_base + 4) + 0x20 = code_base + 0x24
# But string is at 0x28... off by 4

# Let me recalculate:
# Instruction 0: offset 0x00 (movz)
# Instruction 1: offset 0x04 (adr)  ← PC = 0x04, adr target = 0x04 + 0x28 = 0x2C
# Instruction 2: offset 0x08 (loop: ldrb)
# Instruction 3: offset 0x0C (cbz)
# Instruction 4: offset 0x10 (ldrb FR)
# Instruction 5: offset 0x14 (tst)
# Instruction 6: offset 0x18 (b.ne → back to 0x08, offset = -4 insns)
# Instruction 7: offset 0x1C (strb)
# Instruction 8: offset 0x20 (b → back to 0x08, offset = -6 insns)
# Instruction 9: offset 0x24 (halt: wfi)
# Instruction 10: offset 0x28 (b halt → back to 0x24, offset = -1 insn)
# String at: offset 0x2C

# So ADR should point to 0x2C. From PC=0x04, offset = 0x2C - 0x04 = 0x28
# ADR imm = 0x28, immlo = 0, immhi = (0x28 >> 2) << 5 = 10 << 5 = 0x140
# insn = 0x10000000 | 0x140 | 1 = 0x10000141

# OK let me regenerate the code properly
code = bytearray()

# Instruction 0: movz x0, #0x1000, lsl #16
code += struct.pack('<I', 0xD2A20000)

# Instruction 1: adr x1, #0x28  (target = PC + 0x28 = 0x04 + 0x28 = 0x2C)
# immlo = 0, immhi = (0x28 >> 2) << 5 = 0x140
code += struct.pack('<I', 0x10000141)

# Instruction 2 (loop): ldrb w8, [x1], #1
code += struct.pack('<I', 0x38401428)

# Instruction 3: cbz w8, halt (offset = +2 insns = +8 bytes from PC=0x0C → 0x14)
# CBZ: 0x34000000 | (imm19 << 5) | Rt, imm19 = 2
code += struct.pack('<I', 0x34000408)

# Instruction 4: ldrb w9, [x0, #0x18]
code += struct.pack('<I', 0x39406009)

# Instruction 5: tst w9, #0x20 (ANDS WZR, W9, #0x20)
code += struct.pack('<I', 0x7200413F)

# Instruction 6: b.ne loop (from PC=0x18 to 0x08, offset = -4 insns)
# B.cond: 0x54000000 | (imm19 << 5) | cond
# cond NE = 1, imm19 = -4 = 0x7FFFC
code += struct.pack('<I', 0x54FFFE21)

# Instruction 7: strb w8, [x0]
code += struct.pack('<I', 0x39000008)

# Instruction 8: b loop (from PC=0x20 to 0x08, offset = -6 insns)
# B: 0x14000000 | (imm26 & 0x3FFFFFF), imm26 = -6 = 0x3FFFFFA
code += struct.pack('<I', 0x17FFFFFA)

# Instruction 9 (halt): wfi
code += struct.pack('<I', 0xD503207F)

# Instruction 10: b halt (from PC=0x28 to 0x24, offset = -1 insn)
code += struct.pack('<I', 0x17FFFFFE)

# String data at offset 0x2C
code += TEXT

# Pad to 512 bytes for clean loading
while len(code) < 512:
    code += b'\x00'

with open(sys.argv[1], 'wb') as f:
    f.write(code)

print(f"Generated {len(code)} byte kernel at {sys.argv[1]}")
print(f"String 'Hello SiliconV!' at offset 0x2C")
print(f"Entry point: code_base + 0x00")
