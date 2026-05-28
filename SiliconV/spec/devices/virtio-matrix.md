# SiliconV Virtio Device Matrix v0

> Frozen: 2026-05-29
> Status: Draft — Phase 0 Specification Freeze

## Device Status

| Device | v0 Required | Transport | IRQ | MMIO Base | Notes |
|--------|-------------|-----------|-----|-----------|-------|
| virtio-blk | ✅ YES | MMIO | 40 | `0x20000000` | Root filesystem |
| virtio-net | ✅ YES | MMIO | 41 | `0x20010000` | Network connectivity |
| virtio-input | ✅ YES | MMIO | 42 | `0x20020000` | Touchscreen + keyboard |
| virtio-gpu | ✅ YES | MMIO | 43 | `0x20030000` | Framebuffer |
| virtio-console | ✅ YES | MMIO | 44 | `0x20040000` | Guest ↔ Host channel |
| virtio-fs | ⚠️ OPTIONAL | MMIO | 45 | `0x20050000` | 9p shared filesystem |
| virtio-rng | ⚠️ OPTIONAL | MMIO | 46 | `0x20060000` | Entropy source |
| virtio-snd | ❌ v1 | MMIO | 47 | `0x20070000` | Audio (future) |
| virtio-crypto | ❌ v1 | MMIO | 48 | `0x20080000` | Crypto offload (future) |
| virtio-vsock | ❌ v1 | MMIO | 49 | `0x20090000` | Host↔Guest socket (future) |

## Transport

v0 uses **MMIO transport** exclusively.

| Property | Value |
|----------|-------|
| Transport | `virtio,mmio` |
| Config space offset | `0x100` |
| Notification offset | `0x50` |
| ISR offset | `0x60` |
| Device-specific config | `0x100+` |

> PCI transport deferred to v1. MMIO is simpler, easier to debug, and sufficient for Android.

## Feature Negotiation

Each device negotiates features at init time. v0 mandates a **minimal feature set**:

### virtio-blk (Block Device)

| Feature | Required | Description |
|---------|----------|-------------|
| VIRTIO_BLK_F_SIZE_MAX | ✅ | Max segment size |
| VIRTIO_BLK_F_SEG_MAX | ✅ | Max number of segments |
| VIRTIO_BLK_F_GEOMETRY | ✅ | Disk geometry |
| VIRTIO_BLK_F_RO | ✅ | Read-only device |
| VIRTIO_BLK_F_BLK_SIZE | ✅ | Block size |
| VIRTIO_BLK_F_FLUSH | ✅ | Cache flush command |
| VIRTIO_BLK_F_DISCARD | ❌ | Deferred to v1 |
| VIRTIO_BLK_F_WRITE_ZEROES | ❌ | Deferred to v1 |

### virtio-net (Network Device)

| Feature | Required | Description |
|---------|----------|-------------|
| VIRTIO_NET_F_MAC | ✅ | MAC address |
| VIRTIO_NET_F_STATUS | ✅ | Link status |
| VIRTIO_NET_F_MTU | ✅ | Negotiable MTU |
| VIRTIO_NET_F_CSUM | ❌ | Deferred to v1 |
| VIRTIO_NET_F_GUEST_CSUM | ❌ | Deferred to v1 |
| VIRTIO_NET_F_CTRL_VQ | ❌ | Deferred to v1 |

### virtio-input (Input Device)

| Feature | Required | Description |
|---------|----------|-------------|
| (none required) | — | Input is featureless by default |

The host provides absolute pointer events (touchscreen) and key events.

### virtio-gpu (GPU / Framebuffer)

| Feature | Required | Description |
|---------|----------|-------------|
| VIRTIO_GPU_F_VIRGL | ❌ | 3D — deferred to v1 |
| VIRTIO_GPU_F_EDID | ✅ | EDID for display info |
| VIRTIO_GPU_F_RESOURCE_UUID | ❌ | Deferred to v1 |
| VIRTIO_GPU_F_CONTEXT_INIT | ❌ | Deferred to v1 |

> v0 GPU is **2D framebuffer only**. No 3D, no Vulkan, no shaders.

## Device Initialization Sequence

```
1. Hypervisor writes MMIO region
2. Guest reads MagicValue (0x74726976) → confirms Virtio
3. Guest reads Version → must be 2 (modern)
4. Guest reads DeviceID → identifies device type
5. Guest writes Status → ACKNOWLEDGE, then DRIVER
6. Feature negotiation
7. Guest writes Status → FEATURES_OK
8. Configure queues (virtqueue)
9. Guest writes Status → DRIVER_OK
10. Device is live
```

## Host Backend Mapping

| Virtio Device | Host Backend |
|---------------|-------------|
| virtio-blk | Raw image file or block device |
| virtio-net | TAP device or user-mode networking |
| virtio-input | evdev / SDL events / custom input source |
| virtio-gpu | SDL window / headless framebuffer dump |
| virtio-console | Unix socket or pty |
| virtio-fs | virtiofsd (9p server) |
| virtio-rng | /dev/urandom |

## Compliance

A Virtio device is SiliconV v0 compliant if:

- [ ] MagicValue = `0x74726976`
- [ ] Version = `2` (modern)
- [ ] DeviceID matches spec
- [ ] MMIO base address matches `spec/memory/mmio-map.md`
- [ ] IRQ number matches `spec/irq/irq-map.md`
- [ ] Feature negotiation follows per-device requirements above
