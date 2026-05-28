# SiliconV

A virtual phone hardware specification and hypervisor platform.

**SiliconV is not an emulator. It defines a virtual hardware interface.**

## What Is This?

SiliconV defines a **virtual ARM64 phone platform** — a consistent hardware abstraction that lets you:

- Boot unmodified Linux/Android kernels
- Transplant ROM images between devices
- Run Android in a VM with near-native performance
- Develop and test without physical hardware

## Architecture

```
┌─────────────────────────────────────┐
│          Android / Linux            │
├─────────────────────────────────────┤
│            SVABI v0                 │  ← SiliconV ABI (hardware contract)
├─────────────────────────────────────┤
│     Hypervisor (KVM/HVF/WHPX)      │
├─────────────────────────────────────┤
│          Host OS / Hardware         │
└─────────────────────────────────────┘
```

## Current Phase

**Phase 0: Specification Freeze**

Defining the virtual hardware before writing any code.

→ See [spec/](spec/) for the current specifications.

## Quick Start

Nothing to build yet. Read the specs:

- [SVABI v0](spec/svabi/svabi-v0.md) — the ABI contract
- [MMIO Layout](spec/memory/mmio-map.md) — device address map
- [IRQ Layout](spec/irq/irq-map.md) — interrupt assignments
- [Boot Flow](spec/boot/boot-flow.md) — boot sequence
- [DTB Schema](spec/boot/dtb-schema.md) — device tree format
- [Virtio Matrix](spec/devices/virtio-matrix.md) — required devices

## License

TBD
