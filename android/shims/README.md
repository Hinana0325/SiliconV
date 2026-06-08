# android/shims — HAL Shim Layer

Provides HAL (Hardware Abstraction Layer) stubs for Android frameworks that expect physical hardware SiliconV doesn't have.

## Why Shims?

Android expects hardware-specific HALs (display, audio, sensors, camera, etc.). SiliconV is virtual — it has no physical display controller, audio codec, or accelerometer. Shims bridge the gap by implementing HAL interfaces backed by virtual devices.

## Shimmed HALs

| HAL | Module ID | Backend | Status |
|-----|-----------|---------|--------|
| HWComposer | `hwcomposer` | virtio-gpu + drm_hwcomposer | 🔄 |
| Gralloc | `gralloc` | DMABUF heaps (minigbm) | 🔄 |
| Audio | `audio` | Stub (no audio yet) | 🔲 |
| Sensors | `sensors` | Stub (no sensors yet) | 🔲 |
| Camera | `camera` | Stub (no camera yet) | 🔲 |
| Power | `power` | Stub (always on) | 🔲 |

## Files

| File | Description |
|------|-------------|
| `shims.h` | HAL module IDs and shim interface definitions |
| `gralloc.h` | Gralloc buffer allocation structures |
| `hwc_hal.h` | HWComposer HAL layer definitions |

## Architecture

```
Android Framework
    ↓ (HIDL/AIDL)
HAL Manager
    ↓
┌─────────────────────────────┐
│  SiliconV Shim Layer         │
│  ┌──────────┬──────────┐   │
│  │ gralloc  │ hwc_hal  │   │
│  │ (minigbm)│(drm_hwc) │   │
│  └──────────┴──────────┘   │
└─────────────────────────────┘
    ↓ (virtio-gpu / DMABUF)
  SiliconV Hypervisor
```

## Adding a New Shim

1. Define the HAL module ID in `shims.h`
2. Implement the HAL interface header (e.g., `gralloc.h`)
3. Write the shim implementation in `android/shims/`
4. Register the shim in the Android init `.rc` file
5. Add SELinux policy in `android/sepolicy/`
