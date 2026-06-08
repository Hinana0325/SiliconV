# core/irq — GICv3 Emulation

Full GICv3 (Generic Interrupt Controller v3) implementation for the SiliconV virtual machine.

## Overview

The GIC is responsible for routing interrupts from devices to vCPUs. SiliconV emulates a complete GICv3 with:

- **GICD** (Distributor) — global interrupt routing
- **GICR** (Redistributor) — per-CPU interrupt state
- **ITS** (Interrupt Translation Service) — minimal, for MSI support

## Interrupt Map

| Range | Type | Description |
|-------|------|-------------|
| 0–15 | SGI | Software Generated Inter-processor interrupts |
| 16–31 | PPI | Private Peripheral Interrupts (per-CPU) |
| 32–1019 | SPI | Shared Peripheral Interrupts (devices) |

Device IRQ assignments are defined in [spec/irq/irq-map.md](../spec/irq/irq-map.md).

## MMIO Regions

| Base | Size | Component |
|------|------|-----------|
| `0x08000000` | 64KB | GICD (Distributor) |
| `0x08040000` | 128KB | GICR (Redistributor, per-CPU) |
| `0x08080000` | 64KB | ITS (optional) |

## Files

| File | Description |
|------|-------------|
| `gic.c` / `gic.h` | GICv3 emulation implementation |

## Reference

- ARM GIC Architecture Specification v3.0 / v4.0
- Linux kernel: `drivers/irqchip/irq-gic-v3.c`
