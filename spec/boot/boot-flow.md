# SiliconV Boot Flow v0

> Frozen: 2026-05-29
> Status: Draft — Phase 0 Specification Freeze

## Overview

SiliconV boots an ARM64 Linux kernel on a virtual Cortex-A55/A78 machine.
The boot chain is designed for **ROM transplant** compatibility with Android boot images.

## Boot Sequence

```
┌─────────────┐
│  BootROM     │  ← Hypervisor loads to Flash region (0x00000000)
│  (SiliconV   │    Initializes UART, basic GIC, jumps to Loader
│   Loader)    │
└──────┬──────┘
       ▼
┌─────────────┐
│  UEFI /     │  ← Optional: loads kernel + initramfs from virtio-blk
│  Bootloader │    Can be skipped for direct kernel boot (dev mode)
└──────┬──────┘
       ▼
┌─────────────┐
│  Linux      │  ← Decompresses, initializes subsystems
│  Kernel     │    Reads DTB from Flash or passed pointer
│  (arm64)    │
└──────┬──────┘
       ▼
┌─────────────┐
│  Initramfs  │  ← Minimal rootfs, mounts real root
│  (optional) │    Skipped if rootfs is on virtio-blk directly
└──────┬──────┘
       ▼
┌─────────────┐
│  /sbin/init │  ← Android init or systemd (dev mode)
│  (Android   │    Parses init.rc, starts zygote, surfaceflinger
│   Init)     │
└─────────────┘
```

## Boot Modes

### 1. Direct Kernel Boot (Development)

Fastest path. No bootloader.

```
hypervisor loads:
  - kernel Image    → Guest RAM at 0x400000000 + offset
  - DTB             → Guest RAM at kernel + 2MB
  - initramfs       → Guest RAM at DTB + 2MB (optional)

entry point: 0x400000000 + offset
```

**Use case:** Fast iteration, CI, testing.

### 2. Bootloader Boot (Production)

Full chain: BootROM → UEFI → Kernel.

```
hypervisor loads:
  - BootROM image   → Flash at 0x00000000
  - UEFI firmware   → Flash at 0x00000000 + offset

BootROM jumps to UEFI → UEFI loads kernel from virtio-blk
```

**Use case:** ROM transplant, Android production images.

### 3. Android Boot Image

Parses standard Android boot.img header.

```
hypervisor parses boot.img:
  - kernel          → Guest RAM
  - ramdisk         → Guest RAM
  - DTB             → Guest RAM (or appended dtb)
  - cmdline         → Passed to kernel
```

**Use case:** Running actual Android vendor images.

## Kernel Command Line (Default)

```
console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw
```

For Android:
```
console=ttyAMA0 earlycon=pl011,0x10000000 androidboot.hardware=siliconv
  androidboot.selinux=permissive root=/dev/vda rw
```

## DTB Passing

| Mode | DTB Location |
|------|-------------|
| Direct boot | Pointer in x0 at kernel entry |
| UEFI boot | Passed via EFI configuration table |
| Android boot | Appended to kernel or in boot.img header |

## BootROM Specification (v0 Minimal)

The SiliconV BootROM is a **minimal first-stage loader**:

1. Initialize UART0 (PL011) at 115200 baud
2. Initialize GIC (disable all interrupts)
3. Load next-stage image from Flash offset
4. Jump to next stage with x0 = DTB pointer

BootROM prints:
```
SiliconV BootROM v0.1
Loading...
```

## Notes

- Direct kernel boot is the **primary development mode**.
- UEFI is optional — a minimal stub is sufficient for v0.
- Android boot.img parsing happens in the hypervisor launcher, not in guest.
- BootROM is a single flat binary, max 64K.
