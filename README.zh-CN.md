# SiliconV

[English](README.md) | [中文](README.zh-CN.md)

虚拟手机硬件规范与 Hypervisor 平台。

**SiliconV 不是模拟器。它定义了一套虚拟硬件接口。**

## 这是什么？

SiliconV 定义了一套**虚拟 ARM64 手机平台**——一致的硬件抽象层，让你可以：

- 启动未经修改的 Linux/Android 内核
- 在 VM 中以接近原生性能运行 AOSP Android
- 无需物理硬件即可开发和测试
- 在设备间移植 ROM 镜像（未来）

## 硬件栈（v0 — 已冻结）

```
宿主:     KVM (Linux) / HVF (macOS)
内核:     Android Common Kernel 6.6
GPU:      VirGL (Mesa + virglrenderer)
存储:     Virtio-BLK
网络:     Virtio-NET
显示:     virtio-gpu + minigbm + drm_hwcomposer
```

## 架构

```
┌─────────────────────────────────────────────────┐
│                 Android (AOSP GSI)               │
├──────────┬──────────┬──────────┬────────────────┤
│ SurfaceFlinger │ HWComposer │ gralloc │    EGL/GLES   │
│  (AOSP)       │(drm_hwcomp)│(minigbm)│   (Mesa virgl)│
├──────────┴──────────┴──────────┴────────────────┤
│              Linux Kernel 6.6 (ARM64)            │
│    Binder │ Ashmem(memfd) │ DMABUF │ DRM/KMS    │
├─────────────────────────────────────────────────┤
│              SVABI v0 (硬件契约)                   │
│  GICv3 │ PL011 │ Virtio-BLK/NET/GPU/INPUT/CONSOLE│
├─────────────────────────────────────────────────┤
│              SiliconV Hypervisor                  │
│         KVM (Linux) / HVF (macOS)                │
├─────────────────────────────────────────────────┤
│              宿主 OS / 硬件                       │
└─────────────────────────────────────────────────┘
```

## 项目结构

```
├── spec/              # 硬件规范（已冻结）
│   ├── svabi/         #   ABI 契约
│   ├── memory/        #   MMIO 布局
│   ├── irq/           #   IRQ 分配
│   ├── devices/       #   Virtio 设备矩阵
│   └── boot/          #   启动流程 + DTB 模式
│
├── core/              # 平台核心
│   ├── vm/            #   机器 + 启动桩 + bootimg 解析器
│   ├── irq/           #   GICv3 模拟
│   ├── memory/        #   DTB 生成器
│   └── object/        #   PSCI（CPU 生命周期）
│
├── hypervisor/        # 硬件虚拟化
│   ├── abstraction/   #   后端接口
│   └── kvm/           #   KVM 后端 (ARM64)
│
├── devices/           # 设备模拟
│   ├── uart/          #   PL011 串口控制台
│   ├── virtio-blk/    #   块设备
│   └── transport/     #   Virtio MMIO 传输层
│
├── android/           # Android 集成
│   ├── aosp/          #   AOSP 指南
│   ├── binder/        #   Binder/Ashmem/DMABUF
│   ├── graphics/      #   图形管线
│   ├── init/          #   Android init
│   ├── shims/         #   HAL 垫片层
│   └── sepolicy/      #   SELinux 策略
│
├── kernel/            # 内核配置
│   └── configs/       #   android.config (6.6)
│
├── scripts/           # 构建和测试脚本
├── frontend/          # CLI 启动器
├── docs/              # 文档
└── .github/           # CI/CD 工作流
```

## 快速开始

### 构建（宿主）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 构建 Android 内核

```bash
./scripts/build_kernel.sh android14-6.6
```

### 使用 QEMU 测试（需要 ARM64 交叉编译器）

```bash
./scripts/test_qemu.sh
```

### 在 ARM64 宿主上运行

```bash
./build/siliconv -k Image -r rootfs.img
```

## 开发状态

| 阶段 | 状态 | 描述 |
|------|------|------|
| Phase 0 | ✅ | 规范冻结 |
| Phase 1 | ✅ | Hello SiliconV（UART + KVM） |
| Phase 2 | ✅ | 完整 Linux 启动（GICv3 + PSCI + virtio-blk） |
| Phase 3 | 🔄 | Android 内核（Binder + DMABUF + init） |
| Phase 4 | — | AOSP Init 启动（logcat） |
| Phase 5 | — | SurfaceFlinger（黑屏） |
| Phase 6 | — | 第一束光（Launcher 可见） |

## 规范文档

- [SVABI v0](spec/svabi/svabi-v0.md) — ABI 契约
- [硬件栈](spec/hardware-stack.md) — 冻结的硬件选型
- [MMIO 布局](spec/memory/mmio-map.md) — 设备地址映射
- [IRQ 布局](spec/irq/irq-map.md) — 中断分配
- [启动流程](spec/boot/boot-flow.md) — 启动序列
- [DTB 模式](spec/boot/dtb-schema.md) — 设备树格式
- [Virtio 矩阵](spec/devices/virtio-matrix.md) — 必需设备

## 参与贡献

SiliconV 遵循以下原则：

1. **AOSP 优先** — 在 AOSP 稳如磐石之前，不碰厂商 ROM
2. **复用而非重写** — minigbm、drm_hwcomposer、Mesa、virglrenderer
3. **规范先行** — 每次变更都从规范更新开始
4. **VirGL 先于 Venus** — 先通过 VirGL 实现 3D，Vulkan 留到以后

## 许可证

待定
