# SiliconV — AOSP 集成指南

> 策略：AOSP GSI 优先，在 SiliconV 稳定之前永远不碰厂商 ROM。

## 为什么 AOSP 优先

| 方案 | 风险 | 结果 |
|------|------|------|
| HyperOS/OneUI/ColorOS 优先 | 厂商 HAL 地狱、私有服务、破损的 SELinux | 死亡螺旋 |
| **AOSP GSI 优先** | 干净的 HAL、标准 Binder、成熟的 Virtio | **工程基础** |

## AOSP GSI（通用系统镜像）

SiliconV 以 **AOSP ARM64 GSI** 作为第一个 Android 工作负载。

- Android 13 (API 33) — 稳定、经过充分测试
- Android 14 (API 34) — 最新稳定版
- Android 15 (API 35) — 最新版

GSI 意味着：**标准系统镜像，不需要厂商 blob**。

## Cuttlefish 兼容性

SiliconV 目标是在硬件层面实现 **Cuttlefish 兼容**：

| Cuttlefish 设备 | SiliconV 等价物 | 状态 |
|-----------------|----------------|------|
| crosvm (VMM) | SiliconV hypervisor | 进行中 |
| virtio-blk | virtio-blk | ✅ |
| virtio-net | virtio-net | TODO |
| virtio-gpu (virgl) | virtio-gpu (virgl) | TODO |
| virtio-input | virtio-input | TODO |
| virtio-console | virtio-console | TODO |
| GICv3 | GICv3 | ✅ |
| PL011 UART | PL011 UART | ✅ |
| PSCI | PSCI | ✅ |

## 启动链（AOSP）

```
SiliconV Hypervisor
  → BootROM / UEFI 桩
    → Linux 内核（带 Android 补丁）
      → init（第一阶段）
        → init（第二阶段）
          → zygote
            → system_server
              → SurfaceFlinger
                → Launcher
```

## 可复用的关键 AOSP 组件

| 组件 | 用途 | 来源 |
|------|------|------|
| `device/google/cuttlefish` | 设备配置、fstab、init 脚本 | AOSP |
| `external/minigbm` | virtio-gpu 的 gralloc 实现 | AOSP |
| `external/drm_hwcomposer` | 通过 DRM/KMS 的 HWComposer | AOSP |
| `external/virglrenderer` | VirGL 3D 渲染 | AOSP / freedesktop |
| `hardware/google/gfxstream` | 图形流协议 | AOSP |
| `system/core/init` | Android init | AOSP |

## 开发顺序

```
阶段 1：Linux ARM64 启动          ← 你在这里
阶段 2：Android 内核补丁          ← Binder、ashmem、ION
阶段 3：AOSP init 启动            ← logcat 可用
阶段 4：SurfaceFlinger 启动       ← 黑屏但 SF 运行
阶段 5：Virtio-GPU + VirGL        ← Launcher 可见
阶段 6：输入 + 交互               ← 触摸可用
阶段 7：性能优化                   ← 可用
阶段 8：厂商 ROM（可选）           ← 仅在以上全部正常后
```

## 不要做的事

- ❌ 不要自己写 gralloc — 使用 minigbm
- ❌ 不要自己写 HWComposer — 使用 drm_hwcomposer
- ❌ 不要自己写 EGL — 使用 mesa/ANGLE
- ❌ 在 AOSP 完美运行之前不要碰厂商 ROM
- ❌ 在 VirGL 正常工作之前不要实现 Venus（Vulkan）
- ❌ 在管线无头运行正常之前不要加 GUI

## 参考

- [Cuttlefish](https://github.com/google/android-cuttlefish)
- [crosvm](https://github.com/google/crosvm)
- [AOSP GSI](https://source.android.com/docs/setup/create/gsi)
- [minigbm](https://android.googlesource.com/platform/external/minigbm/)
- [drm_hwcomposer](https://android.googlesource.com/platform/external/drm_hwcomposer/)
- [virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer)
