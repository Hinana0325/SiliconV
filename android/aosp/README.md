# SiliconV — AOSP Integration Guide

> Strategy: AOSP GSI first, vendor ROMs never until SiliconV is stable.

## Why AOSP First

| Approach | Risk | Outcome |
|----------|------|---------|
| HyperOS/OneUI/ColorOS first | Vendor HAL hell, private services, broken SELinux | Death spiral |
| **AOSP GSI first** | Clean HAL, standard Binder, mature Virtio | **Engineering foundation** |

## AOSP GSI (Generic System Image)

SiliconV targets **AOSP ARM64 GSI** as the first Android workload.

- Android 13 (API 33) — stable, well-tested
- Android 14 (API 34) — latest stable
- Android 15 (API 35) — latest

GSI means: **standard system image, no vendor blobs required**.

## Cuttlefish Compatibility

SiliconV aims to be **Cuttlefish-compatible** at the hardware level:

| Cuttlefish Device | SiliconV Equivalent | Status |
|-------------------|-------------------|--------|
| crosvm (VMM) | SiliconV hypervisor | In progress |
| virtio-blk | virtio-blk | ✅ |
| virtio-net | virtio-net | ✅ | machine.c integration, IRQ 41 |
| virtio-gpu (virgl) | virtio-gpu (virgl) | TODO | Phase 5, MMIO 0x20030000 |
| virtio-input | virtio-input | TODO | Phase 7 |
| virtio-console | virtio-console | ✅ | Guest-Host channel, IRQ 44 |
| GICv3 | GICv3 | ✅ |
| PL011 UART | PL011 UART | ✅ |
| PSCI | PSCI | ✅ |

## Boot Chain (AOSP)

```
SiliconV Hypervisor
  → BootROM / UEFI stub
    → Linux kernel (with Android patches)
      → init (first_stage)
        → init (second_stage)
          → zygote
            → system_server
              → SurfaceFlinger
                → Launcher
```

## Key AOSP Components to Reuse

| Component | Purpose | Source |
|-----------|---------|--------|
| `device/google/cuttlefish` | Device config, fstab, init scripts | AOSP |
| `external/minigbm` | gralloc implementation for virtio-gpu | AOSP |
| `external/drm_hwcomposer` | HWComposer via DRM/KMS | AOSP |
| `external/virglrenderer` | VirGL 3D rendering | AOSP / freedesktop |
| `hardware/google/gfxstream` | Graphics stream protocol | AOSP |
| `system/core/init` | Android init | AOSP |

## Development Order

```
Phase 1: Linux ARM64 boot          ← You are here
Phase 2: Android kernel patches    ← Binder, ashmem, ION
Phase 3: AOSP init boot            ← logcat works
Phase 4: SurfaceFlinger start      ← Black screen but SF running
Phase 5: Virtio-GPU + VirGL        ← Launcher visible
Phase 6: Input + interaction       ← Touch works
Phase 7: Performance optimization  ← Usable
Phase 8: Vendor ROM (optional)     ← Only after all above works
```

## What NOT to Do

- ❌ Don't write your own gralloc — use minigbm
- ❌ Don't write your own HWComposer — use drm_hwcomposer
- ❌ Don't write your own EGL — use mesa/ANGLE
- ❌ Don't touch vendor ROMs until AOSP works perfectly
- ❌ Don't implement Venus (Vulkan) before VirGL works
- ❌ Don't add GUI before the pipeline works headless

## References

- [Cuttlefish](https://github.com/google/android-cuttlefish)
- [crosvm](https://github.com/google/crosvm)
- [AOSP GSI](https://source.android.com/docs/setup/create/gsi)
- [minigbm](https://android.googlesource.com/platform/external/minigbm/)
- [drm_hwcomposer](https://android.googlesource.com/platform/external/drm_hwcomposer/)
- [virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer)
