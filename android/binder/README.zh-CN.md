# android/binder — Binder IPC 与内存接口

Android 核心 IPC 和内存共享机制的头文件定义。

## 概述

Android 框架依赖三个内核驱动来实现 IPC 和内存管理：

| 驱动 | 设备 | 用途 |
|------|------|------|
| **Binder** | `/dev/binder`, `/dev/hwbinder`, `/dev/vndbinder` | 进程间通信 |
| **Ashmem** | `/dev/ashmem` | 匿名共享内存（旧版，6.6 中被 memfd 替代） |
| **DMABUF/ION** | `/dev/ion` 或 DMABUF 堆 | GPU/相机/视频的 DMA 缓冲区分配 |

## 文件

| 文件 | 描述 |
|------|------|
| `binder.h` | Binder 协议常量、事务结构 |
| `ashmem.h` | Ashmem ioctl 定义 |
| `dmabuf.h` | ION/DMABUF 堆 ID 和标志 |
| `android_dtb.h` | Android 特定设备树节点的 DTB 片段 |

## SiliconV 方案

SiliconV **不在** hypervisor 中模拟这些。而是：

1. 客户内核内置 binder/ashmem/DMABUF（`CONFIG_ANDROID_BINDER_IPC=y`）
2. 设备通过 DTB 配置（binder 设备名称、DMABUF 堆设置）
3. hypervisor 只需确保 DTB 包含正确的 Android 特定节点

## Binder 设备映射

| 设备 | 用途 | 使用者 |
|------|------|--------|
| `/dev/binder` | 框架 IPC | 系统服务器、应用 |
| `/dev/hwbinder` | HAL IPC | HIDL 接口 |
| `/dev/vndbinder` | 厂商 IPC | 厂商 HAL |

## 参考

- `drivers/android/binder.c`（Linux 内核）
- `drivers/dma-buf/heaps/`（DMABUF 堆）
