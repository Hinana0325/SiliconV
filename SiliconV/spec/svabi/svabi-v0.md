# SiliconV ABI (SVABI) v0

> Frozen: 2026-05-29
> Status: Draft — Phase 0 Specification Freeze

## What is SVABI?

SVABI (SiliconV Application Binary Interface) defines the **contract between hypervisor and guest**.

It specifies:
- How the hypervisor presents hardware to the guest
- How the guest communicates with devices
- What the guest can expect at boot time
- The minimal hardware surface a guest OS needs

## ABI Version

```
SVABI_MAJOR = 0
SVABI_MINOR = 1
```

Version is embedded in:
- DTB `compatible` string: `"siliconv,vm-v0"`
- BootROM banner: `"SiliconV BootROM v0.1"`

## Guest Architecture

| Property | Value |
|----------|-------|
| ISA | AArch64 (ARMv8.0-A minimum) |
| Exception level at boot | EL1 (kernel) |
| Page size | 4K |
| Physical address space | 48-bit |
| Endianness | Little-endian |

## Boot Register State

At kernel entry (direct boot mode):

| Register | Value | Description |
|----------|-------|-------------|
| x0 | DTB physical address | Pointer to flattened device tree |
| x1–x3 | 0 | Reserved |
| SP | Valid stack | Bootloader sets up minimal stack |
| SCTLR_EL1 | Reset value | MMU off, caches off |
| Current EL | EL1 | Kernel runs at EL1 |

## PSCI Interface

The guest uses PSCI (via HVC) for CPU lifecycle:

| Function | PSCI ID | Description |
|----------|---------|-------------|
| CPU_ON | 0x84000003 | Bring up secondary CPU |
| CPU_OFF | 0x84000002 | Shut down current CPU |
| SYSTEM_RESET | 0x84000009 | Reboot |
| SYSTEM_OFF | 0x84000008 | Power off |

## Memory Layout Summary

```
0x0000_0000 ─┬─ Flash / BootROM      (128M)
             │
0x0800_0000 ─┼─ GIC                  (192K)
             │
0x1000_0000 ─┼─ Platform devices     (64K each)
             │  UART, RTC, Watchdog
             │
0x2000_0000 ─┼─ Virtio devices       (64K each)
             │  BLK, NET, INPUT, GPU, CONSOLE, FS, RNG
             │
0x4000_0000 ─┼─ Platform Bus         (256M)
             │
0x5000_0000 ─┼─ PCI (optional)       (256M)
             │
   ... gap ...
             │
0x4_0000_0000 ┼─ Guest RAM            (4G default)
```

## Conformance Levels

### Level 0 (Minimum — v0)

A SiliconV implementation passes Level 0 if it can:

1. Create an ARM64 VM with 4 Cortex-A55 cores and 4G RAM
2. Generate a valid DTB matching `spec/boot/dtb-schema.md`
3. Emulate UART0 at `0x10000000` (PL011 compatible)
4. Emulate GICv3 at `0x08000000`
5. Emulate at least virtio-blk (rootfs) and virtio-gpu (framebuffer)
6. Boot a Linux ARM64 kernel to shell (init or Android init)
7. Handle PSCI CPU_ON/OFF for secondary CPUs

### Level 1 (Full v0)

All of Level 0, plus:

8. All 5 required Virtio devices functional
9. virtio-fs and virtio-rng working
10. RTC and Watchdog functional
11. Android boot image support
12. ROM transplant capability (toolchain)

## References

- [ARM GICv3 Specification](https://developer.arm.com/documentation/ihi0069/latest)
- [Virtio 1.2 Specification](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
- [PSCI Specification](https://developer.arm.com/documentation/den0022/latest)
- [Linux ARM64 Boot Requirements](https://www.kernel.org/doc/Documentation/arm64/booting.txt)
