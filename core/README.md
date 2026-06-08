# core — Platform Core

The heart of SiliconV. Contains the VM manager, interrupt controller, device tree generator, and CPU lifecycle management.

## Modules

| Module | Description |
|--------|-------------|
| [vm/](vm/) | VM machine loop, boot image parser, boot stub |
| [irq/](irq/) | GICv3 interrupt controller emulation |
| [memory/](memory/) | DTB (Device Tree Blob) runtime generator |
| [object/](object/) | PSCI — CPU power state coordination |
| [dma/](dma/) | DMA buffer management |
| [scheduler/](scheduler/) | vCPU scheduler |

## Architecture

```
┌─────────────────────────────────────┐
│           VM Machine (machine.c)     │
│  ┌────────┬────────┬──────────────┐ │
│  │  GICv3 │  PSCI  │  DTB Gen     │ │
│  │ (irq/) │(object/)│ (memory/)   │ │
│  └────────┴────────┴──────────────┘ │
│  ┌────────────────────────────────┐ │
│  │  Boot Image Parser (bootimg.c) │ │
│  └────────────────────────────────┘ │
└─────────────────────────────────────┘
```

## Key Design Decisions

- **Direct boot** — no bootloader needed; parse boot.img and jump to kernel
- **Runtime DTB** — generate device tree from spec at launch, not at build time
- **GICv3 only** — no legacy GICv2 support; simplifies code and matches modern kernels
- **PSCI via HVC** — guest kernel calls PSCI through HVC (Hypervisor Call), not SMC
