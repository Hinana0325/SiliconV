# spec/irq — IRQ Layout

Defines interrupt assignments for all virtual devices.

## Documents

| Document | Description |
|----------|-------------|
| [irq-map.md](irq-map.md) | Complete IRQ allocation table |

## IRQ Summary

| IRQ | Type | Device |
|-----|------|--------|
| 0–15 | SGI | Software Generated (IPI) |
| 16–31 | PPI | Private per-CPU interrupts |
| 32 | SPI | PL011 UART |
| 33 | SPI | Virtio-Blk |
| 34 | SPI | Virtio-Net |
| 35 | SPI | Virtio-GPU |
| 36 | SPI | Virtio-Input |
| 37 | SPI | Virtio-Console |

## Rules

- SPI IRQs start at 32 (GIC_SPI_BASE)
- IRQs are allocated sequentially per device
- Each virtio device gets one IRQ for all virtqueues
