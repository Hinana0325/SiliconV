# SiliconV — Android Graphics Pipeline

> The hardest part of Android virtualization is not CPU or memory.
> It's the graphics stack.

## Pipeline Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Android App                             │
│                     (Canvas / OpenGL / Vulkan)               │
├─────────────────────────────────────────────────────────────┤
│                     Android Framework                       │
│                     (WindowManager / View)                   │
├─────────────────────────────────────────────────────────────┤
│                     SurfaceFlinger                           │
│                     (compositor, vsync, layers)              │
├──────────────┬──────────────┬───────────────────────────────┤
│  HWComposer  │   Gralloc    │    EGL / GLES / Vulkan       │
│  (display)   │  (buffers)   │    (GPU rendering)           │
├──────────────┼──────────────┼───────────────────────────────┤
│  drm_hwcomposer │  minigbm  │    Mesa (virgl / venus)      │
│  (DRM/KMS)   │ (dmabuf)    │    (OpenGL / Vulkan driver)  │
├──────────────┴──────────────┴───────────────────────────────┤
│                   DRM/KMS (kernel)                           │
│                   virtio-gpu driver                          │
├─────────────────────────────────────────────────────────────┤
│                   SiliconV Hypervisor                        │
│                   virtio-gpu backend                         │
│                   virglrenderer (3D)                         │
│                   or framebuffer (2D)                        │
├─────────────────────────────────────────────────────────────┤
│                   Host Display (SDL / headless)              │
└─────────────────────────────────────────────────────────────┘
```

## The Five Layers

### 1. SurfaceFlinger (Android compositor)

- Receives buffers from apps via BufferQueue
- Composites layers into a single framebuffer
- Triggers vsync
- Sends composited frame to HWComposer

**Status:** Runs on standard Android kernel. No SiliconV-specific work needed.

### 2. HWComposer (Display HAL)

- Receives composited frame from SurfaceFlinger
- Sends to display hardware (or virtual display)
- Manages vsync timing

**SiliconV approach:** Use `drm_hwcomposer` (AOSP standard).
It talks to the kernel DRM/KMS API, which talks to virtio-gpu.

### 3. Gralloc (Buffer allocation)

- Allocates graphics buffers for apps and SurfaceFlinger
- Must produce dmabuf file descriptors
- Must support the formats Android needs (RGBA8888, NV12, etc.)

**SiliconV approach:** Use `minigbm` (AOSP standard).
It allocates from DMA-buf heaps, which the kernel provides.

### 4. EGL / GLES / Vulkan (GPU rendering)

- Apps render into gralloc buffers using OpenGL ES or Vulkan
- Mesa provides the GPU driver

**SiliconV approach:**
- v0: **VirGL** (OpenGL over virtio) — more mature, works today
- v1: **Venus** (Vulkan over virtio) — better performance, more complex

### 5. virtio-gpu (Hypervisor GPU)

- Kernel driver presents a DRM/KMS device
- Forwards rendering commands to hypervisor
- Hypervisor uses virglrenderer (3D) or raw framebuffer (2D)

**SiliconV approach:**
- 2D mode: raw framebuffer, no 3D (fast, simple, Phase 4)
- 3D mode: virglrenderer, OpenGL commands forwarded (Phase 5)

## Development Phases

### Phase 4: Framebuffer (2D)

```
SurfaceFlinger → HWComposer → DRM/KMS → virtio-gpu (2D mode) → SDL window
```

- No 3D, no Vulkan
- Launcher renders via software composition
- **Milestone:** Black screen with cursor, or basic launcher

### Phase 5: VirGL (3D)

```
SurfaceFlinger → HWComposer → DRM/KMS → virtio-gpu (virgl) → virglrenderer → host GPU
```

- OpenGL ES 2.0/3.0 via VirGL
- Apps can use GPU rendering
- **Milestone:** Smooth launcher, apps render correctly

### Phase 6: Venus (Vulkan) — Future

```
SurfaceFlinger → HWComposer → DRM/KMS → virtio-gpu (venus) → venus driver → host Vulkan
```

- Vulkan 1.1 passthrough
- Better performance for games and GPU-heavy apps
- **Milestone:** Vulkan apps work

## What to Reuse (NOT Rewrite)

| Component | Source | Why |
|-----------|--------|-----|
| minigbm | `external/minigbm` | Battle-tested gralloc for virtio-gpu |
| drm_hwcomposer | `external/drm_hwcomposer` | Standard HWC2 implementation |
| Mesa | `external/mesa3d` | OpenGL/Vulkan drivers (virgl, venus) |
| virglrenderer | `external/virglrenderer` | 3D command translation |
| DRM/KMS kernel | Linux kernel | Standard display API |

## Key Insight

**You don't write the graphics stack. You wire existing components together.**

The SiliconV hypervisor provides the virtio-gpu device.
The Linux kernel provides the DRM/KMS driver.
Mesa provides the OpenGL/Vulkan drivers.
minigbm provides gralloc.
drm_hwcomposer provides HWComposer.
SurfaceFlinger is stock AOSP.

Your job: make sure the virtio-gpu backend works correctly.
