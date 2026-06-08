# kernel — 内核配置

SiliconV 的 Android Common Kernel 构建配置和补丁。

## 概述

SiliconV 使用 **Android Common Kernel**（android14-6.6），配合自定义配置覆盖层，启用：

- Android 特定驱动（Binder、Ashmem、DMABUF）
- SiliconV 虚拟设备的 Virtio 驱动
- ARM64 虚拟化功能（KVM、EL2）
- DRM/KMS 显示输出

## 文件

| 文件 | 描述 |
|------|------|
| [configs/android.config](configs/android.config) | SiliconV 的内核配置覆盖层 |
| [dtb/](dtb/) | 预构建的设备树 blob（可选） |
| [modules/](modules/) | 可加载内核模块 |
| [patches/](patches/) | 内核补丁（如需要） |

## 构建内核

```bash
# 使用提供的脚本
./scripts/build_kernel.sh android14-6.6

# 或手动：
git clone --branch android14-6.6 https://android.googlesource.com/kernel/common
cd common
make ARCH=arm64 defconfig
scripts/kconfig/merge_config.sh .config /path/to/kernel/configs/android.config
make ARCH=arm64 -j$(nproc)
```

## 关键配置项

| 配置 | 值 | 用途 |
|------|----|----|
| `CONFIG_ANDROID_BINDER_IPC` | y | Binder IPC 驱动 |
| `CONFIG_ANDROID_BINDER_DEVICES` | "binder,hwbinder,vndbinder" | Binder 设备节点 |
| `CONFIG_MEMFD` | y | memfd（6.6 中替代 Ashmem） |
| `CONFIG_DMABUF_HEAPS` | y | DMABUF 堆分配器 |
| `CONFIG_VIRTIO` | y | Virtio 总线支持 |
| `CONFIG_VIRTIO_BLK` | y | Virtio 块设备 |
| `CONFIG_VIRTIO_NET` | y | Virtio 网络 |
| `CONFIG_VIRTIO_GPU` | y | Virtio GPU |
| `CONFIG_DRM_VIRTIO_GPU` | y | DRM virtio-gpu 驱动 |

## 内核镜像

构建产出：
- `Image` — 未压缩的 ARM64 内核镜像
- `Image.gz` — 压缩的内核镜像
- `dtbs/` — 设备树 blob（SiliconV 在运行时生成自己的）

## 参考

- Android 内核：https://source.android.com/docs/core/architecture/kernel
- 内核配置：https://android.googlesource.com/kernel/configs/
