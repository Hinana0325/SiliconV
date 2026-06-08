# spec/memory — Memory Layout

Defines the guest physical address map (MMIO layout).

## Documents

| Document | Description |
|----------|-------------|
| [mmio-map.md](mmio-map.md) | Complete MMIO address allocation |

## Address Map Summary

```
0x00000000 - 0x3FFFFFFF   SRAM / Boot ROM (unused)
0x08000000 - 0x080FFFFF   GICv3 (Distributor + Redistributor)
0x10000000 - 0x10000FFF   PL011 UART
0x10001000 - 0x10005FFF   Virtio-MMIO devices (4KB each)
0x400000000+              Guest RAM (default 4GB)
```

## Rules

- All MMIO regions are 4KB aligned
- Virtio devices are spaced at 4KB intervals starting at `0x10001000`
- Guest RAM base is `0x400000000` (16GB mark) to allow for device space below
