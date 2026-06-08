# gpu — GPU & Graphics

GPU virtualization and graphics rendering components.

## Overview

SiliconV's graphics pipeline uses **VirGL** (Virtual 3D GPU) to provide OpenGL ES support to the guest. The guest's Mesa virgl driver sends rendering commands to the host, where virglrenderer executes them on the host GPU.

## Architecture

```
Guest (Android App)
    ↓ OpenGL ES calls
Mesa virgl (guest driver)
    ↓ VirGL command stream
virtio-gpu transport
    ↓ (hypervisor boundary)
virglrenderer (host)
    ↓ OpenGL/EGL
Host GPU (hardware)
```

## Modules

| Module | Description | Status |
|--------|-------------|--------|
| [virgl/](virgl/) | VirGL protocol and command handling | 🔄 |
| [vulkan/](vulkan/) | Vulkan passthrough (Venus) | 🔲 |
| [venus/](venus/) | Venus protocol (Vulkan over virtio) | 🔲 |
| [renderer/](renderer/) | Host-side rendering backend | 🔲 |
| [shaders/](shaders/) | Shader translation/compilation | 🔲 |

## Rendering Path

**Phase 1 (Current): VirGL**
- Mesa virgl driver in guest → virglrenderer on host
- OpenGL ES 2.0 / 3.0 support
- Software fallback available

**Phase 2 (Future): Venus**
- Mesa venus driver in guest → Vulkan passthrough
- Near-native Vulkan performance
- Requires host Vulkan support

## Dependencies

- **Guest**: Mesa with virgl driver (built into AOSP)
- **Host**: virglrenderer, Mesa/EGL, GPU drivers
- **Transport**: virtio-gpu (see `devices/virtio-gpu/`)

## Reference

- virglrenderer: https://gitlab.freedesktop.org/virgl/virglrenderer
- Mesa virgl: https://docs.mesa3d.org/drivers/virgl.html
- Venus: https://docs.mesa3d.org/drivers/venus.html
