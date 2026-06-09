# SiliconV Build Guide

> Complete guide to building and running SiliconV.

## Quick Start (ARM64 Host)

```bash
# 1. Build hypervisor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 2. Get kernel + rootfs (see below)

# 3. Validate configuration without starting vCPUs
./build/sv-cli --dry-run -k Image -r rootfs.img

# 4. Run
./build/sv-cli -k Image -r rootfs.img
```

## Build Options

### Option A: Full Build (Recommended)

```bash
./scripts/build_all.sh
```

This will:
1. Build the SiliconV hypervisor
2. Build the Android kernel (if cross-compiler available)
3. Prepare rootfs from AOSP GSI (if system.img present)

### Option B: Step-by-Step

#### 1. Build Hypervisor

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake

# ARM64 cross-compiler (for kernel build)
sudo apt install gcc-aarch64-linux-gnu

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

#### 2. Build Android Kernel

```bash
# Requires ARM64 cross-compiler
./scripts/build_kernel.sh android14-6.6

# Output: kernel/out/Image
```

#### 3. Prepare Rootfs

**From AOSP GSI (Full Android):**

```bash
# Download GSI
./scripts/download_gsi.sh

# Place system.img in build/gsi/

# Create rootfs (requires root)
sudo ./scripts/prepare_rootfs.sh
```

**Minimal Test Rootfs:**

```bash
# Quick test rootfs with busybox (requires root)
sudo ./scripts/create_minimal_rootfs.sh

# Output: build/rootfs-minimal.img
```

## Docker Build

For reproducible builds:

```bash
# Build Docker image
docker build -t siliconv-build docker/

# Run build inside container
docker run -v $(pwd):/workspace -it siliconv-build \
    ./scripts/build_all.sh --skip-gsi
```

## Running

### Smoke Test Without a Hypervisor Backend

```bash
./build/sv-cli --dry-run \
    -k kernel/out/Image \
    -r build/rootfs.img \
    -m 1024 \
    -n 2
```

`--dry-run` is useful on x86_64 CI and developer machines because it validates kernel loading, optional rootfs attachment, DTB generation, and device setup without requiring ARM64 KVM/HVF.

### On ARM64 Linux (with KVM)

```bash
./build/sv-cli \
    -k kernel/out/Image \
    -r build/rootfs.img \
    -m 4096 \
    -n 4
```

### With QEMU (for testing)

```bash
# Basic test
./scripts/test_qemu.sh

# Manual QEMU
qemu-system-aarch64 \
    -machine virt,gic-version=3 \
    -cpu cortex-a55 \
    -smp 4 \
    -m 4G \
    -kernel kernel/out/Image \
    -drive file=build/rootfs.img,if=virtio \
    -nographic \
    -append "console=ttyAMA0 root=/dev/vda rw"
```

## AOSP GSI Images

Download from: https://developer.android.com/topic/generic-system-image/releases

| Version | Architecture | Variant | Size |
|---------|-------------|---------|------|
| Android 14 | ARM64 | userdebug | ~2GB |
| Android 15 | ARM64 | userdebug | ~2GB |

After download, extract to `build/gsi/`.

## File Structure

```
build/
├── sv-cli                    # Hypervisor binary
├── rootfs.img                # Bootable rootfs
├── rootfs-minimal.img        # Test rootfs (busybox)
├── gsi/
│   └── system.img            # AOSP GSI system image
└── mount/                    # Temporary mount point

kernel/
├── build/                    # Kernel source (cloned)
└── out/
    └── Image                 # Built kernel

docker/
└── Dockerfile                # Build environment
```

## Troubleshooting

### "Cross-compiler not found"

```bash
sudo apt install gcc-aarch64-linux-gnu
export CROSS_COMPILE=aarch64-linux-gnu-
```

### "system.img not found"

Download AOSP GSI manually:
1. Visit https://developer.android.com/topic/generic-system-image/releases
2. Select ARM64 + userdebug
3. Extract to `build/gsi/`

### "KVM not available"

SiliconV requires ARM64 Linux with KVM. On x86_64, use QEMU for testing.
The hypervisor binary compiles on x86_64 but KVM backend is ARM64-only.

### "Permission denied" on rootfs

Rootfs preparation requires root (for mount/umount):
```bash
sudo ./scripts/prepare_rootfs.sh
```
