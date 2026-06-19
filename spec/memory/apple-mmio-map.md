# Apple Profile — MMIO Memory Map

This document defines the MMIO address layout for the Apple virtual platform
profile (SVABI-Apple v0).

## Address Layout

| Region | Base | Size | Device |
|--------|------|------|--------|
| **AIC** | `0x03000000` | 64 KB | Apple Interrupt Controller |
| **UART0** | `0x03010000` | 64 KB | Apple S5L UART (console) |
| **UART1** | `0x03020000` | 64 KB | Apple S5L UART (debug) |
| **DART0** | `0x03030000` | 64 KB | IOMMU (display DMA) |
| **DART1** | `0x03040000` | 64 KB | IOMMU (storage/NVMe DMA) |
| **SEP** | `0x03050000` | 2 MB | Secure Enclave Processor |
| **WDT** | `0x03250000` | 64 KB | Watchdog Timer |
| **NVRAM** | `0x03260000` | 64 KB | NVRAM Controller |
| **TIMER** | `0x03270000` | 64 KB | Apple Timer |
| **GPIO** | `0x03280000` | 64 KB | GPIO Controller |
| **I2C0** | `0x03290000` | 64 KB | I2C Bus 0 |
| **SPMI** | `0x032A0000` | 64 KB | SPMI Controller |
| **PCIe** | `0x032B0000` | 64 KB | PCIe Host Bridge (stub) |
| **NVMe** | `0x032C0000` | 128 KB | NVMe Storage Controller |
| | | | |
| **Virtio-BLK** | `0x20000000` | 64 KB | Block device (#0) |
| **Virtio-NET** | `0x20010000` | 64 KB | Network device |
| **Virtio-CONSOLE** | `0x20040000` | 64 KB | Console channel |
| **Virtio-BLK1** | `0x20050000` | 64 KB | Block device (#1) |
| | | | |
| **Guest RAM** | `0x800000000` | up to 16 GB |

## Address Conflicts

The Apple profile addresses (0x030xxxxx) are chosen to avoid conflicts with
the Android profile (0x08xxxxxx for GIC, 0x10xxxxxx for UART, 0x20xxxxxx for
Virtio). This allows both profiles to coexist in the same binary.

In the Apple profile:
- GICv3 region (0x08000000) is UNUSED — AIC replaces it
- Android UART region (0x10000000) is UNUSED — Apple UART replaces it

## Device Address Constants

These addresses are defined as C macros in:
`core/memory/mmio_addrs.h` (Apple section)

## Notes

- All devices use 64 KB MMIO regions for consistency
- SEP uses a larger 2 MB region to accommodate mailbox + coprocessor state
- Reserved ranges can be assigned to future devices without breaking compatibility
