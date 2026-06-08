# android — Android 集成

在 SiliconV 中启动和运行 AOSP Android 所需的一切。

## 模块

| 模块 | 描述 | 状态 |
|------|------|------|
| [aosp/](aosp/) | AOSP 构建和集成指南 | ✅ |
| [binder/](binder/) | Binder IPC、Ashmem、DMABUF 接口 | 🔄 |
| [graphics/](graphics/) | 图形管线（Mesa virgl → SurfaceFlinger） | ✅ |
| [init/](init/) | Android init 系统配置 | ✅ |
| [shims/](shims/) | HAL 垫片层（hwcomposer、gralloc 等） | 🔄 |
| [sepolicy/](sepolicy/) | SiliconV 的 SELinux 策略 | 🔲 |
| [hal/](hal/) | 硬件抽象层桩 | 🔲 |
| [surfaceflinger/](surfaceflinger/) | SurfaceFlinger 配置 | 🔲 |
| [vendor/](vendor/) | 厂商特定覆盖 | 🔲 |

## Android 启动序列

```
1. SiliconV 从 boot.img 加载 kernel + ramdisk
2. 内核启动 → 挂载 initramfs
3. /init 启动 → 解析 init.rc
4. Init 启动 servicemanager（Binder）
5. Init 启动 SurfaceFlinger（显示）
6. Init 启动 Zygote（应用框架）
7. Launcher 出现
```

## 图形管线

```
应用（OpenGL ES）
    ↓
EGL/GLES（Mesa virgl）
    ↓
SurfaceFlinger（合成器）
    ↓
HWComposer（drm_hwcomposer 通过 virtio-gpu）
    ↓
显示（virtio-gpu → 宿主窗口）
```

## 关键设计决策

- **AOSP GSI** — 使用通用系统镜像；在 AOSP 稳定之前不碰厂商 ROM
- **VirGL 优先** — 通过 VirGL（Mesa + virglrenderer）实现 3D；Vulkan（Venus）留到以后
- **标准 HAL** — 复用上游 Mesa、drm_hwcomposer、minigbm
- **无自定义内核补丁** — 使用 android common kernel 配合 SiliconV 配置
