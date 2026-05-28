# SiliconV MMIO Layout v0

> Frozen: 2026-05-29
> Status: Draft — Phase 0 Specification Freeze

## Design Principles

1. **Aligned to 64K boundaries** — simplifies page table mapping
2. **GIC at low address** — ARM convention
3. **Virtio devices sequential** — easy enumeration, no gaps
4. **Leave room** — each device gets 64K even if it needs less

## Memory Map

| Region | Base Address | End Address | Size | Description |
|--------|-------------|-------------|------|-------------|
| Flash (BootROM) | `0x00000000` | `0x07FFFFFF` | 128M | Firmware / BootROM image |
| GIC Distributor | `0x08000000` | `0x0800FFFF` | 64K | GICv3 Distributor (GICD) |
| GIC Redistributor | `0x08010000` | `0x0801FFFF` | 64K | GICv3 Redistributor (GICR) |
| GIC ITS | `0x08020000` | `0x0802FFFF` | 64K | GICv3 Interrupt Translation Service |
| UART0 (PL011) | `0x10000000` | `0x1000FFFF` | 64K | Primary serial console |
| RTC | `0x10010000` | `0x1001FFFF` | 64K | Real-time clock |
| Watchdog | `0x10020000` | `0x1002FFFF` | 64K | System watchdog timer |
| Virtio-BLK | `0x20000000` | `0x2000FFFF` | 64K | Block device (rootfs) |
| Virtio-NET | `0x20010000` | `0x2001FFFF` | 64K | Network device |
| Virtio-INPUT | `0x20020000` | `0x2002FFFF` | 64K | Touchscreen / keyboard |
| Virtio-GPU | `0x20030000` | `0x2003FFFF` | 64K | Framebuffer / GPU |
| Virtio-CONSOLE | `0x20040000` | `0x2004FFFF` | 64K | Guest console channel |
| Virtio-FS | `0x20050000` | `0x2005FFFF` | 64K | Shared filesystem (9p) |
| Virtio-RNG | `0x20060000` | `0x2006FFFF` | 64K | Entropy source |
| Reserved (Virtio) | `0x20070000` | `0x200FFFFF` | ~576K | Future Virtio devices |
| Platform Bus | `0x40000000` | `0x4FFFFFFF` | 256M | MMIO passthrough / platform devices |
| PCI ECAM | `0x50000000` | `0x51FFFFFF` | 32M | PCI config space (if PCI enabled) |
| PCI MMIO32 | `0x52000000` | `0x5FFFFFFF` | 224M | PCI MMIO BAR region |

## Guest RAM

| Region | Base Address | Size | Description |
|--------|-------------|------|-------------|
| RAM | `0x400000000` (16G) | Configurable (default 4G) | Guest physical memory |

> RAM starts at 16G to avoid conflict with MMIO region below 4G boundary.
> This matches the `virt` board convention.

## Notes

- All Virtio devices use **MMIO transport** (not PCI) for simplicity in v0.
- PCI support is optional and deferred to v1.
- UART0 is the **primary debug channel** — all early boot output goes here.
- Guest RAM base is at `0x400000000` to keep low 4G clean for device MMIO.
