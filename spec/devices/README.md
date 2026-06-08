# spec/devices — Device Specification

Defines the virtual devices required by SiliconV.

## Documents

| Document | Description |
|----------|-------------|
| [virtio-matrix.md](virtio-matrix.md) | Required virtio devices and their configuration |

## Device Requirements

Every SiliconV implementation MUST support:

| Device | Virtio ID | Transport | Required |
|--------|-----------|-----------|----------|
| Block | 2 | MMIO | ✅ |
| Net | 1 | MMIO | ✅ |
| GPU | 16 | MMIO | ✅ |
| Input | 18 | MMIO | ✅ |
| Console | 3 | MMIO | ✅ |

Plus non-virtio devices:
- PL011 UART (serial console)
- GICv3 (interrupt controller)
- PSCI (CPU lifecycle)
