# devices — Device Emulation

Emulated hardware devices visible to the guest. All devices are memory-mapped (MMIO) and interrupt-driven.

## Device List

| Device | MMIO Base | IRQ | Status |
|--------|-----------|-----|--------|
| [PL011 UART](uart/) | `0x10000000` | 32 | ✅ |
| [Virtio-MMIO Transport](transport/) | `0x10001000`+ | 33+ | ✅ |
| [Virtio-Blk](virtio-blk/) | via transport | via transport | ✅ |
| [Virtio-Net](virtio-net/) | via transport | via transport | 🔲 |
| [Virtio-GPU](virtio-gpu/) | via transport | via transport | 🔲 |
| [Virtio-Input](virtio-input/) | via transport | via transport | 🔲 |
| [Virtio-Console](virtio-console/) | via transport | via transport | 🔲 |

## Architecture

```
Guest Kernel
    │
    ▼ (MMIO read/write)
┌──────────────────────────────┐
│  Virtio-MMIO Transport Layer │  ← transport/
│  ┌──────┬──────┬──────┬───┐ │
│  │ BLK  │ NET  │ GPU  │...│ │  ← individual devices
│  └──────┴──────┴──────┴───┘ │
├──────────────────────────────┤
│  PL011 UART                  │  ← uart/
└──────────────────────────────┘
    │
    ▼ (IRQ injection)
  GICv3 (core/irq/)
```

## Design Principles

- **Virtio 1.2 compliant** — standard drivers work out of the box
- **MMIO transport** — no PCI complexity; simpler and sufficient for ARM64
- **Minimal viable implementation** — implement only the features the guest actually uses
- **MMIO exit driven** — device logic runs when the guest accesses device registers
