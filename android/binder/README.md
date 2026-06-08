# android/binder — Binder IPC & Memory Interfaces

Header definitions for Android's core IPC and memory-sharing mechanisms.

## Overview

Android's framework relies on three kernel drivers for IPC and memory management:

| Driver | Device | Purpose |
|--------|--------|---------|
| **Binder** | `/dev/binder`, `/dev/hwbinder`, `/dev/vndbinder` | IPC between processes |
| **Ashmem** | `/dev/ashmem` | Anonymous shared memory (legacy, replaced by memfd in 6.6) |
| **DMABUF/ION** | `/dev/ion` or DMABUF heaps | DMA buffer allocation for GPU/camera/video |

## Files

| File | Description |
|------|-------------|
| `binder.h` | Binder protocol constants, transaction structures |
| `ashmem.h` | Ashmem ioctl definitions |
| `dmabuf.h` | ION/DMABUF heap IDs and flags |
| `android_dtb.h` | DTB fragment for Android-specific device tree nodes |

## SiliconV Approach

SiliconV does **not** emulate these in the hypervisor. Instead:

1. The guest kernel has binder/ashmem/DMABUF built-in (`CONFIG_ANDROID_BINDER_IPC=y`)
2. Devices are configured via the DTB (binder device names, DMABUF heap setup)
3. The hypervisor only needs to ensure the DTB includes the correct Android-specific nodes

## Binder Device Mapping

| Device | Purpose | Used By |
|--------|---------|---------|
| `/dev/binder` | Framework IPC | System server, apps |
| `/dev/hwbinder` | HAL IPC | HIDL interfaces |
| `/dev/vndbinder` | Vendor IPC | Vendor HALs |

## Reference

- `drivers/android/binder.c` (Linux kernel)
- `drivers/dma-buf/heaps/` (DMABUF heaps)
