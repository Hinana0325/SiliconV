# gpu — GPU 与图形

GPU 虚拟化和图形渲染组件。

## 概述

SiliconV 的图形管线使用 **VirGL**（虚拟 3D GPU）为客户提供 OpenGL ES 支持。客户的 Mesa virgl 驱动将渲染命令发送到宿主，virglrenderer 在宿主 GPU 上执行它们。

## 架构

```
客户（Android 应用）
    ↓ OpenGL ES 调用
Mesa virgl（客户驱动）
    ↓ VirGL 命令流
virtio-gpu 传输
    ↓（hypervisor 边界）
virglrenderer（宿主）
    ↓ OpenGL/EGL
宿主 GPU（硬件）
```

## 模块

| 模块 | 描述 | 状态 |
|------|------|------|
| [virgl/](virgl/) | VirGL 协议和命令处理 | 🔄 |
| [vulkan/](vulkan/) | Vulkan 直通（Venus） | 🔲 |
| [venus/](venus/) | Venus 协议（Vulkan over virtio） | 🔲 |
| [renderer/](renderer/) | 宿主端渲染后端 | 🔲 |
| [shaders/](shaders/) | 着色器翻译/编译 | 🔲 |

## 渲染路径

**阶段 1（当前）：VirGL**
- Mesa virgl 驱动（客户）→ virglrenderer（宿主）
- OpenGL ES 2.0 / 3.0 支持
- 可用软件回退

**阶段 2（未来）：Venus**
- Mesa venus 驱动（客户）→ Vulkan 直通
- 接近原生的 Vulkan 性能
- 需要宿主 Vulkan 支持

## 依赖

- **客户**：带 virgl 驱动的 Mesa（内置于 AOSP）
- **宿主**：virglrenderer、Mesa/EGL、GPU 驱动
- **传输**：virtio-gpu（见 `devices/virtio-gpu/`）

## 参考

- virglrenderer：https://gitlab.freedesktop.org/virgl/virglrenderer
- Mesa virgl：https://docs.mesa3d.org/drivers/virgl.html
- Venus：https://docs.mesa3d.org/drivers/venus.html
