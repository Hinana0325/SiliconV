# devices/uart — PL011 UART

ARM PL011 UART emulation for guest serial console output. This is the primary debug and early-boot output channel.

## Overview

The PL011 is a standard ARM UART. SiliconV implements the minimal register set needed for:

- Character output (guest → host terminal)
- Character input (host keyboard → guest)
- Basic interrupt signaling

## MMIO Layout

| Address | Register | Description |
|---------|----------|-------------|
| `0x10000000` | DR | Data Register (read/write) |
| `0x10000018` | FR | Flag Register (TX/RX status) |
| `0x10000024` | IBRD | Integer Baud Rate |
| `0x10000028` | FBRD | Fractional Baud Rate |
| `0x1000002C` | LCR_H | Line Control |
| `0x10000030` | CR | Control Register |
| `0x10000038` | IMSC | Interrupt Mask |
| `0x10000044` | ICR | Interrupt Clear |

IRQ: **32** (first SPI)

## Files

| File | Description |
|------|-------------|
| `pl011.c` / `pl011.h` | PL011 UART emulation |

## Guest Connection

The UART is connected to the host's stdout/stdin by default. In CLI mode, characters flow directly to the terminal. In GUI mode (Cocoa/Qt), they route to a terminal widget.

## Reference

- ARM PrimeCell UART (PL011) Technical Reference Manual
- Linux kernel: `drivers/tty/serial/amba-pl011.c`
