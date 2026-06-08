# android/shims — HAL 垫片层

为期望物理硬件但 SiliconV 不具备的 Android 框架提供 HAL 桩。

## 为什么需要垫片？

Android 期望硬件特定的 HAL（显示、音频、传感器、相机等）。SiliconV 是虚拟的——没有物理显示控制器、音频编解码器或加速度计。垫片层通过用虚拟设备后端实现 HAL 接口来弥合差距。

## 垫片化的 HAL

| HAL | 模块 ID | 后端 | 状态 |
|-----|---------|------|------|
| HWComposer | `hwcomposer` | virtio-gpu + drm_hwcomposer | 🔄 |
| Gralloc | `gralloc` | DMABUF 堆（minigbm） | 🔄 |
| Audio | `audio` | 桩（暂无音频） | 🔲 |
| Sensors | `sensors` | 桩（暂无传感器） | 🔲 |
| Camera | `camera` | 桩（暂无相机） | 🔲 |
| Power | `power` | 桩（始终开启） | 🔲 |

## 文件

| 文件 | 描述 |
|------|------|
| `shims.h` | HAL 模块 ID 和垫片接口定义 |
| `gralloc.h` | Gralloc 缓冲区分配结构 |
| `hwc_hal.h` | HWComposer HAL 层定义 |

## 架构

```
Android 框架
    ↓ (HIDL/AIDL)
HAL 管理器
    ↓
┌─────────────────────────────┐
│  SiliconV 垫片层             │
│  ┌──────────┬──────────┐   │
│  │ gralloc  │ hwc_hal  │   │
│  │ (minigbm)│(drm_hwc) │   │
│  └──────────┴──────────┘   │
└─────────────────────────────┘
    ↓ (virtio-gpu / DMABUF)
  SiliconV Hypervisor
```

## 添加新垫片

1. 在 `shims.h` 中定义 HAL 模块 ID
2. 实现 HAL 接口头文件（如 `gralloc.h`）
3. 在 `android/shims/` 中编写垫片实现
4. 在 Android init `.rc` 文件中注册垫片
5. 在 `android/sepolicy/` 中添加 SELinux 策略
