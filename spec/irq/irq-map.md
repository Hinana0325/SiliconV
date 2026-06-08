# SiliconV IRQ Layout v0

> Frozen: 2026-05-29
> Status: Draft — Phase 0 Specification Freeze

## Design Principles

1. **SPI (Shared Peripheral Interrupts)** only for device IRQs
2. **PPI (Private Peripheral Interrupts)** for per-CPU timers
3. **No IRQ sharing** — each device gets a unique SPI number
4. **Sequential allocation** — easy to reason about, no magic numbers

## ARM GICv3 Interrupt Numbers

> IRQ numbers below are SPI, starting at SPI base (32).

### System Peripherals

| IRQ | Device | Type | Trigger | Description |
|-----|--------|------|---------|-------------|
| 1 | Virtual Timer | PPI | Level | ARM generic timer (EL1) |
| 2 | Hypervisor Timer | PPI | Level | ARM generic timer (EL2) |
| 13 | Legacy FIQ | PPI | Level | Secure interrupt (if needed) |

### Platform Devices

| IRQ | Device | Type | Trigger | Description |
|-----|--------|------|---------|-------------|
| 32 | UART0 | SPI | Level | PL011 serial console |
| 33 | RTC | SPI | Level | Real-time clock |
| 34 | Watchdog | SPI | Level | System watchdog |

### Virtio Devices

| IRQ | Device | Type | Trigger | Description |
|-----|--------|------|---------|-------------|
| 40 | Virtio-BLK | SPI | Level | Block device |
| 41 | Virtio-NET | SPI | Level | Network device |
| 42 | Virtio-INPUT | SPI | Level | Input device |
| 43 | Virtio-GPU | SPI | Level | GPU / framebuffer |
| 44 | Virtio-CONSOLE | SPI | Level | Guest console |
| 45 | Virtio-FS | SPI | Level | Shared filesystem |
| 46 | Virtio-RNG | SPI | Level | Entropy source |
| 47–55 | Reserved | SPI | — | Future Virtio devices |

### PCI (if enabled)

| IRQ | Device | Type | Trigger | Description |
|-----|--------|------|---------|-------------|
| 56–63 | PCI Legacy (INTx A-D) | SPI | Level | PCI interrupt lines |

## IRQ Mapping in Device Tree

```dts
/ {
    interrupt-parent = <&gic>;

    uart@10000000 {
        interrupts = <GIC_SPI 32 IRQ_TYPE_LEVEL_HIGH>;
    };

    virtio_blk@20000000 {
        interrupts = <GIC_SPI 40 IRQ_TYPE_LEVEL_HIGH>;
    };

    virtio_net@20010000 {
        interrupts = <GIC_SPI 41 IRQ_TYPE_LEVEL_HIGH>;
    };
};
```

## Notes

- All SPIs are **active-high level triggered** by default.
- No MSI support in v0 (deferred to PCI/v1).
- IRQ numbers are **fixed**, not dynamically allocated. Guest DTB must match.
- Future devices use the reserved range 47–55.
