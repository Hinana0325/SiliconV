# devices/transport — Virtio MMIO Transport

Implements the virtio-mmio transport layer — the bus that connects virtio devices to the guest kernel.

## Overview

Each virtio device is exposed as an MMIO region. The guest kernel discovers devices by reading device IDs from fixed MMIO addresses. The transport handles:

- Device initialization and feature negotiation
- Virtqueue setup and notification
- Interrupt injection on completion

## MMIO Register Layout

Each device occupies a 4KB MMIO region with this register map:

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| `0x000` | MagicValue | R | `0x74726976` ("virt") |
| `0x004` | Version | R | `2` (modern virtio) |
| `0x008` | DeviceID | R | Device type (1=net, 2=blk, 16=gpu, ...) |
| `0x00C` | VendorID | R | `0x554D4551` ("QEMU") |
| `0x010` | DeviceFeatures | R | Feature bits offered |
| `0x020` | DriverFeatures | W | Feature bits accepted |
| `0x030` | QueueSel | W | Select virtqueue |
| `0x034` | QueueNumMax | R | Max queue size |
| `0x038` | QueueNum | W | Queue size |
| `0x044` | QueueReady | RW | Queue ready flag |
| `0x050` | QueueNotify | W | Notify device |
| `0x060` | InterruptStatus | R | Pending interrupts |
| `0x064` | InterruptAck | W | Acknowledge interrupt |

## Device Address Allocation

Devices are placed at `0x10001000` with 4KB spacing:

```
0x10001000  virtio-blk  (DeviceID=2)
0x10002000  virtio-net  (DeviceID=1)
0x10003000  virtio-gpu  (DeviceID=16)
0x10004000  virtio-input (DeviceID=18)
0x10005000  virtio-console (DeviceID=3)
```

## Files

| File | Description |
|------|-------------|
| `virtio_mmio.c` / `virtio_mmio.h` | Transport layer implementation |

## Reference

- Virtio 1.2 Specification, Section 4.2.4 (Legacy/MMIO)
- Linux kernel: `drivers/virtio/virtio_mmio.c`
