# SiliconV v0 — Frozen Hardware Stack

> Frozen: 2026-05-29
> These choices are final for v0. Do not change.

## Host

| Component | Choice | Notes |
|-----------|--------|-------|
| Hypervisor (Linux) | **KVM** | ARM64 hosts |
| Hypervisor (macOS) | **HVF** | Apple Silicon |
| Hypervisor (Windows) | WHPX | Future, not v0 |

## Guest Kernel

| Component | Choice | Notes |
|-----------|--------|-------|
| Kernel | **Android Common Kernel 6.6** | `android14-6.6` branch |
| Kernel config | `kernel/configs/android.config` | Frozen |

## Graphics

| Component | Choice | Notes |
|-----------|--------|-------|
| GPU API | **VirGL** | OpenGL ES over virtio |
| 3D Backend | **virglrenderer** | Mesa/freedesktop |
| Display | virtio-gpu (2D + 3D) | DRM/KMS in guest |
| Gralloc | **minigbm** | AOSP standard |
| HWComposer | **drm_hwcomposer** | AOSP standard |
| EGL/GLES Driver | **Mesa** | virgl driver |

**NOT using:** Venus (Vulkan) — deferred to v1.

## Storage

| Component | Choice | Notes |
|-----------|--------|-------|
| Block Device | **Virtio-BLK** | MMIO transport |
| Root FS | ext4 | Standard Android |
| Partition | Dynamic partitions (super) | Android 10+ |

## Network

| Component | Choice | Notes |
|-----------|--------|-------|
| Network | **Virtio-NET** | MMIO transport |
| Backend | TAP device | Host networking |
| Alt backend | user-mode | Fallback (no TAP) |

## Other Devices

| Component | Choice | Notes |
|-----------|--------|-------|
| UART | PL011 | Primary console |
| Input | Virtio-INPUT | Touchscreen + keyboard |
| Console | Virtio-CONSOLE | Guest ↔ Host |
| Entropy | Virtio-RNG | /dev/urandom |
| Shared FS | Virtio-FS | Optional |

## Summary (One-Liner)

```
KVM/HVF + AOSP 6.6 kernel + VirGL + Virtio-BLK + Virtio-NET
```

## What This Means

- **No Venus/Vulkan** — VirGL only for v0
- **No ION** — DMABUF heaps (6.6 kernel)
- **No ASHMEM** — memfd (6.6 kernel)
- **No vendor ROMs** — AOSP GSI only
- **No custom HAL** — reuse minigbm, drm_hwcomposer, Mesa
