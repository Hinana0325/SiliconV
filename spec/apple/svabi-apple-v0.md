# SVABI-Apple v0 — Apple Virtual Platform Profile

## Overview

This document defines the Apple profile extension to SiliconV's SVABI v0
specification. It enables running Apple XNU (iOS/macOS) kernels as guest
operating systems.

## Key Differences from Android Profile

| Aspect | Android (svabi-v0) | Apple (svabi-apple-v0) |
|--------|--------------------|------------------------|
| Interrupt Controller | GICv3 | Apple AIC |
| Serial Console | PL011 UART | Apple S5L UART |
| IOMMU | None | DART (2 instances) |
| Security Enclave | None | SEP (simplified) |
| Watchdog | Generic PL031 | Apple WDT |
| NVRAM | None | Apple NVRAM |
| Boot Image | Android boot.img | IMG4 / raw Mach-O |
| Device Tree | Standard FDT | Apple DeviceTree (DTRE) |
| Kernel Entry | Linux boot protocol | XNU boot protocol |
| RAM Base | 0x400000000 | 0x800000000 |

## Hardware Stack

| Layer | Selection |
|-------|-----------|
| Guest Architecture | AArch64 (ARMv8.4-A) |
| Guest Kernel | XNU (iOS 15+/macOS 12+) |
| Interrupt Controller | Apple AIC v1 |
| Serial | Apple S5L UART (2 ports) |
| Storage | Virtio-BLK (MMIO) |
| Network | Virtio-NET (MMIO) |
| IOMMU | Apple DART (2 instances) |
| Security | Apple SEP (simplified emulation) |

## MMIO Memory Layout

See [apple-mmio-map.md](../memory/apple-mmio-map.md) for full details.

## IRQ Mapping

See [apple-irq-map.md](../irq/apple-irq-map.md) for full details.

## Boot Flow

See [apple-boot-flow.md](../boot/apple-boot-flow.md) for full details.

## Device Compatibility

### Implemented Devices (v0)

- Apple AIC (Interrupt Controller)
- Apple S5L UART (Serial Console)
- Apple DART (IOMMU)
- Apple SEP (Secure Enclave, simplified)
- Apple WDT (Watchdog Timer)
- Apple NVRAM
- Virtio-BLK (Block Device)
- Virtio-NET (Network Device)
- Virtio-CONSOLE (Console Channel)

### Not Yet Implemented (v0 — Stubbed)

- Apple DisplayPipe (Framebuffer)
- Apple GPIO / I2C / SPI
- Apple PCIe
- Apple AOP (Always-On Processor)
- Apple PMU (Power Management)

## Limitations (v0)

1. Boot chain bypass: iBoot/WTF/LLB/iBSS/iBEC are NOT emulated.
   XNU kernel is loaded directly via IMG4 parser or raw Mach-O loader.
2. SEP runs in simplified emulation mode (sep-sim), not real SEPFW.
3. No GPU acceleration — 2D framebuffer only.
4. No hardware-accelerated crypto (AES engine stubbed).
5. TCG backend only (KVM/HVF on Apple Silicon hosts may be added later).
