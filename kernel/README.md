# kernel — Kernel Configuration

Android Common Kernel build configuration and patches for SiliconV.

## Overview

SiliconV uses the **Android Common Kernel** (android14-6.6) with a custom config overlay that enables:

- Android-specific drivers (Binder, Ashmem, DMABUF)
- Virtio drivers for SiliconV's virtual devices
- ARM64 virtualization features (KVM, EL2)
- DRM/KMS for display output

## Files

| File | Description |
|------|-------------|
| [configs/android.config](configs/android.config) | Kernel config overlay for Android on SiliconV |
| [dtb/](dtb/) | Pre-built device tree blobs (optional) |
| [modules/](modules/) | Loadable kernel modules |
| [patches/](patches/) | Kernel patches (if needed) |

## Building the Kernel

```bash
# Using the provided script
./scripts/build_kernel.sh android14-6.6

# Or manually:
git clone --branch android14-6.6 https://android.googlesource.com/kernel/common
cd common
make ARCH=arm64 defconfig
scripts/kconfig/merge_config.sh .config /path/to/kernel/configs/android.config
make ARCH=arm64 -j$(nproc)
```

## Key Config Options

| Config | Value | Purpose |
|--------|-------|---------|
| `CONFIG_ANDROID_BINDER_IPC` | y | Binder IPC driver |
| `CONFIG_ANDROID_BINDER_DEVICES` | "binder,hwbinder,vndbinder" | Binder device nodes |
| `CONFIG_MEMFD` | y | memfd (replaces Ashmem in 6.6) |
| `CONFIG_DMABUF_HEAPS` | y | DMABUF heap allocator |
| `CONFIG_VIRTIO` | y | Virtio bus support |
| `CONFIG_VIRTIO_BLK` | y | Virtio block device |
| `CONFIG_VIRTIO_NET` | y | Virtio network |
| `CONFIG_VIRTIO_GPU` | y | Virtio GPU |
| `CONFIG_DRM_VIRTIO_GPU` | y | DRM virtio-gpu driver |

## Kernel Image

The build produces:
- `Image` — uncompressed ARM64 kernel image
- `Image.gz` — compressed kernel image
- `dtbs/` — device tree blobs (SiliconV generates its own at runtime)

## Reference

- Android Kernel: https://source.android.com/docs/core/architecture/kernel
- Kernel configs: https://android.googlesource.com/kernel/configs/
