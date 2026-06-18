# Apple Profile — Boot Flow

## Overview

The Apple boot flow in SiliconV bypasses the real Apple boot chain
(iBoot/WTF/LLB/iBSS/iBEC). Instead, XNU kernel is loaded directly via a
custom boot loader that performs the tasks iBoot normally handles.

## Boot Sequence

```
sv-cli --platform apple -k kernelcache.release.[soc].img4

  │
  ├─ 1. Parse IMG4 container (or load raw Mach-O)
  │     Extract: kernel, DeviceTree, TrustCache, ramdisk
  │
  ├─ 2. Load Apple DeviceTree (DTRE)
  │     Load from IMG4 payload or separate .dtre file
  │     Populate with runtime info (boot-args, memory map, etc.)
  │     Filter nodes (remove unsupported devices)
  │     Serialize to FDT format in guest RAM
  │
  ├─ 3. Load XNU kernel
  │     Parse Mach-O header (MH_FILESET for modern iOS)
  │     Load segments into guest RAM (__TEXT, __DATA, __LINKEDIT, etc.)
  │     Apply ASLR offset
  │     Resolve symbol stubs
  │
  ├─ 4. Load TrustCache
  │     Parse and load into guest RAM
  │     Set DTB reference
  │
  ├─ 5. Load RAM disk (if present)
  │     Restore/RAM disk for recovery mode
  │
  ├─ 6. Set up BootArgs
  │     Write AppleKernelBootArgsRev2/Rev3 structure
  │     Set video info, DTB pointer, command line
  │     Set boot flags (normal boot, dark-boot)
  │
  └─ 7. Start vCPU
        x0 = BootArgs pointer
        PC = kernel entry point
        CPSR = EL1h mode
```

## XNU Kernel Boot Protocol

### Register State at Entry

| Register | Value |
|----------|-------|
| x0 | Pointer to BootArgs structure |
| x1 | 0 (flags, reserved) |
| x2 | 0 (reserved) |
| PC | Kernel entry point (from Mach-O LC_UNIXTHREAD) |
| CPSR | 0x3C5 (EL1h, DAIF masked, AArch64) |

### BootArgs Structure

The BootArgs structure (AppleKernelBootArgsRev2 or Rev3) is placed in a
reserved region of guest RAM (typically at `DRAM_top - 16KB`) and contains:

- Revision (2 or 3 for iOS 17+)
- Version
- Video info (framebuffer base, width, height, depth)
- DeviceTree pointer
- Command line string
- Boot flags (normal, restore, safe-mode)
- Memory map
- Random seeds
- NVRAM proxy data

## Mach-O File Formats

### Modern iOS (iOS 13+): MH_FILESET

Modern XNU kernel caches use the MH_FILESET format:

- Single Mach-O file containing multiple "fileset entries"
- Each entry is a separate Mach-O (kext, kernel core)
- The kernel core entry has LC_UNIXTHREAD with the real entry point
- Filesets allow lazy loading of kexts

### Legacy: MH_EXECUTE

Older iOS and macOS kernels use standard MH_EXECUTE:

- Single Mach-O with all segments
- LC_UNIXTHREAD for entry point
- LC_SEGMENT_64 for __TEXT, __DATA, __LINKEDIT, etc.

## IMG4 Container Format

Standard Apple firmware container format:

```
IMG4 Container:
  Magic: "IMG4"
  ┌─────────────────────────┐
  │ Image4 Manifest (IM4M)  │  ASN.1/DER encoded signature
  ├─────────────────────────┤
  │ Image4 Payload (IM4P)   │  The actual payload (kernel, DTB, etc.)
  │  ├─ Type: "krnl"        │  4-character type code
  │  ├─ Data: ...           │  Raw or compressed data
  │  └─ Compression: LZSS/  │  LZSS or LZFSE
  │     LZFSE/none          │
  ├─────────────────────────┤
  │ Image4 Restore (IM4R)   │  Restore info (optional)
  └─────────────────────────┘
```

## DeviceTree Node Filtering

When populating the DTB, nodes for unimplemented devices should be:
1. Removed entirely if not in the support white-list
2. Or have their `compatible` properties modified to prevent driver matching

Supported compatible strings (v0):
- `aic,1` — Apple AIC
- `uart-1,samsung` — Apple S5L UART
- `dart,s8000` — DART IOMMU
- `wdt` — Apple Watchdog
- `nvram` — Apple NVRAM
- `virtio,mmio` — Virtio devices (extended)
