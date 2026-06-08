# SiliconV — Android 图形管线

> Android 虚拟化最难的部分不是 CPU 或内存。
> 而是图形栈。

## 管线概览

```
┌─────────────────────────────────────────────────────────────┐
│                     Android 应用                            │
│                     (Canvas / OpenGL / Vulkan)               │
├─────────────────────────────────────────────────────────────┤
│                     Android 框架                            │
│                     (WindowManager / View)                   │
├─────────────────────────────────────────────────────────────┤
│                     SurfaceFlinger                           │
│                     (合成器、vsync、图层)                     │
├──────────────┬──────────────┬───────────────────────────────┤
│  HWComposer  │   Gralloc    │    EGL / GLES / Vulkan       │
│  (显示)      │  (缓冲区)    │    (GPU 渲染)                │
├──────────────┼──────────────┼───────────────────────────────┤
│  drm_hwcomposer │  minigbm  │    Mesa (virgl / venus)      │
│  (DRM/KMS)   │ (dmabuf)    │    (OpenGL / Vulkan 驱动)    │
├──────────────┴──────────────┴───────────────────────────────┤
│                   DRM/KMS（内核）                            │
│                   virtio-gpu 驱动                            │
├─────────────────────────────────────────────────────────────┤
│                   SiliconV Hypervisor                        │
│                   virtio-gpu 后端                            │
│                   virglrenderer (3D)                         │
│                   或帧缓冲 (2D)                              │
├─────────────────────────────────────────────────────────────┤
│                   宿主显示（SDL / 无头）                      │
└─────────────────────────────────────────────────────────────┘
```

## 五层架构

### 1. SurfaceFlinger（Android 合成器）

- 通过 BufferQueue 接收来自应用的缓冲区
- 将图层合成到单个帧缓冲
- 触发 vsync
- 将合成帧发送到 HWComposer

**状态：** 在标准 Android 内核上运行。不需要 SiliconV 特定工作。

### 2. HWComposer（显示 HAL）

- 从 SurfaceFlinger 接收合成帧
- 发送到显示硬件（或虚拟显示）
- 管理 vsync 时序

**SiliconV 方案：** 使用 `drm_hwcomposer`（AOSP 标准）。
它通过内核 DRM/KMS API 与 virtio-gpu 通信。

### 3. Gralloc（缓冲区分配）

- 为应用和 SurfaceFlinger 分配图形缓冲区
- 必须产生 dmabuf 文件描述符
- 必须支持 Android 需要的格式（RGBA8888、NV12 等）

**SiliconV 方案：** 使用 `minigbm`（AOSP 标准）。
它从 DMA-buf 堆分配，由内核提供。

### 4. EGL / GLES / Vulkan（GPU 渲染）

- 应用使用 OpenGL ES 或 Vulkan 渲染到 gralloc 缓冲区
- Mesa 提供 GPU 驱动

**SiliconV 方案：**
- v0：**VirGL**（通过 virtio 的 OpenGL）— 更成熟，现在可用
- v1：**Venus**（通过 virtio 的 Vulkan）— 性能更好，更复杂

### 5. virtio-gpu（Hypervisor GPU）

- 内核驱动呈现 DRM/KMS 设备
- 将渲染命令转发到 hypervisor
- Hypervisor 使用 virglrenderer（3D）或原始帧缓冲（2D）

**SiliconV 方案：**
- 2D 模式：原始帧缓冲，无 3D（快速、简单，阶段 4）
- 3D 模式：virglrenderer，转发 OpenGL 命令（阶段 5）

## 开发阶段

### 阶段 4：帧缓冲（2D）

```
SurfaceFlinger → HWComposer → DRM/KMS → virtio-gpu (2D 模式) → SDL 窗口
```

- 无 3D，无 Vulkan
- Launcher 通过软件合成渲染
- **里程碑：** 黑屏带光标，或基本 launcher

### 阶段 5：VirGL（3D）

```
SurfaceFlinger → HWComposer → DRM/KMS → virtio-gpu (virgl) → virglrenderer → 宿主 GPU
```

- 通过 VirGL 的 OpenGL ES 2.0/3.0
- 应用可使用 GPU 渲染
- **里程碑：** 流畅的 launcher，应用正确渲染

### 阶段 6：Venus（Vulkan）— 未来

```
SurfaceFlinger → HWComposer → DRM/KMS → virtio-gpu (venus) → venus 驱动 → 宿主 Vulkan
```

- Vulkan 1.1 直通
- 游戏和 GPU 密集型应用性能更好
- **里程碑：** Vulkan 应用可用

## 复用（而非重写）

| 组件 | 来源 | 原因 |
|------|------|------|
| minigbm | `external/minigbm` | virtio-gpu 经过实战检验的 gralloc |
| drm_hwcomposer | `external/drm_hwcomposer` | 标准 HWC2 实现 |
| Mesa | `external/mesa3d` | OpenGL/Vulkan 驱动（virgl、venus） |
| virglrenderer | `external/virglrenderer` | 3D 命令翻译 |
| DRM/KMS 内核 | Linux 内核 | 标准显示 API |

## 关键洞察

**你不是在写图形栈。你是在把现有组件连接在一起。**

SiliconV hypervisor 提供 virtio-gpu 设备。
Linux 内核提供 DRM/KMS 驱动。
Mesa 提供 OpenGL/Vulkan 驱动。
minigbm 提供 gralloc。
drm_hwcomposer 提供 HWComposer。
SurfaceFlinger 是原生 AOSP。

你的工作：确保 virtio-gpu 后端正确工作。
