# android — Android Integration

Everything needed to boot and run AOSP Android inside SiliconV.

## Modules

| Module | Description | Status |
|--------|-------------|--------|
| [aosp/](aosp/) | AOSP build and integration guide | ✅ |
| [binder/](binder/) | Binder IPC, Ashmem, DMABUF interfaces | 🔄 |
| [graphics/](graphics/) | Graphics pipeline (Mesa virgl → SurfaceFlinger) | ✅ |
| [init/](init/) | Android init system configuration | ✅ |
| [shims/](shims/) | HAL shim layer (hwcomposer, gralloc, etc.) | 🔄 |
| [sepolicy/](sepolicy/) | SELinux policy for SiliconV | 🔲 |
| [hal/](hal/) | Hardware abstraction layer stubs | 🔲 |
| [surfaceflinger/](surfaceflinger/) | SurfaceFlinger configuration | 🔲 |
| [vendor/](vendor/) | Vendor-specific overlays | 🔲 |

## Android Boot Sequence

```
1. SiliconV loads kernel + ramdisk from boot.img
2. Kernel boots → mounts initramfs
3. /init starts → parses init.rc
4. Init starts servicemanager (Binder)
5. Init starts SurfaceFlinger (display)
6. Init starts Zygote (app framework)
7. Launcher appears
```

## Graphics Pipeline

```
App (OpenGL ES)
    ↓
EGL/GLES (Mesa virgl)
    ↓
SurfaceFlinger (compositor)
    ↓
HWComposer (drm_hwcomposer via virtio-gpu)
    ↓
Display (virtio-gpu → host window)
```

## Key Design Decisions

- **AOSP GSI** — use Generic System Image; no vendor ROMs until AOSP works
- **VirGL first** — 3D via VirGL (Mesa + virglrenderer); Vulkan (Venus) later
- **Standard HALs** — reuse upstream Mesa, drm_hwcomposer, minigbm
- **No custom kernel patches** — use android common kernel with SiliconV config
