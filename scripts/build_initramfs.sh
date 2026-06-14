#!/bin/bash
# SiliconV — Build Android First-Stage Initramfs
#
# Compiles the first-stage init and packages it into an initramfs
# cpio archive that the kernel can load via -initrd.
#
# Usage: ./build_initramfs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build/initramfs"

echo "=== SiliconV Initramfs Builder ==="

# Check for cross-compiler
CROSS_CC="${AARCH64_CROSS_COMPILE:-aarch64-linux-gnu-gcc}"
if ! command -v "$CROSS_CC" &>/dev/null; then
    echo "Error: ARM64 cross-compiler not found ($CROSS_CC)"
    echo "Install: apt-get install gcc-aarch64-linux-gnu"
    exit 1
fi

# Build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"/{dev,proc,sys,system_root,tmp,linkerconfig}

# Compile init
echo "  CC   init.c → init (ARM64 static)"
"$CROSS_CC" \
    -static \
    -Os \
    -Wall -Wextra \
    -ffunction-sections -fdata-sections \
    -Wl,--gc-sections \
    -o "$BUILD_DIR/init" \
    "${PROJECT_DIR}/initramfs/init.c"

if [ ! -f "$BUILD_DIR/init" ]; then
    echo "Error: init compilation failed"
    exit 1
fi

# Verify it's ARM64 and static
echo "  FILE $(file "$BUILD_DIR/init")"
echo "  SIZE $(du -h "$BUILD_DIR/init" | cut -f1)"

# Strip to reduce size
aarch64-linux-gnu-strip "$BUILD_DIR/init" 2>/dev/null || true
echo "  SIZE $(du -h "$BUILD_DIR/init" | cut -f1) (stripped)"

# Create device nodes
echo "  Creating device nodes..."
sudo mknod -m 666 "$BUILD_DIR/dev/null" c 1 3 2>/dev/null || true
sudo mknod -m 666 "$BUILD_DIR/dev/console" c 5 1 2>/dev/null || true
sudo mknod -m 600 "$BUILD_DIR/dev/ttyAMA0" c 204 64 2>/dev/null || true

# Include SELinux policy files
mkdir -p "$BUILD_DIR/selinux_policy"

if [ -f "${PROJECT_DIR}/images/precompiled_sepolicy" ]; then
    echo "  Including precompiled_sepolicy (host-compiled)..."
    cp "${PROJECT_DIR}/images/precompiled_sepolicy" "$BUILD_DIR/selinux_policy/"
else
    echo "  WARNING: precompiled_sepolicy not found"
fi

if [ -f "${PROJECT_DIR}/images/plat_sepolicy_patched.cil" ]; then
    echo "  Including patched plat_sepolicy.cil (for hash verification)..."
    cp "${PROJECT_DIR}/images/plat_sepolicy_patched.cil" "$BUILD_DIR/selinux_policy/"
else
    echo "  WARNING: patched plat_sepolicy.cil not found"
fi

# Write the expected sha256 hash for precompiled sepolicy verification
echo "03f08c8d1aff00a2b88730df4cb00d63f44b3e8194d9dc84aa3991d27dd2edeb" > "$BUILD_DIR/selinux_policy/plat_sepolicy_and_mapping.sha256"

# Create initramfs cpio archive
echo "  CPIO initramfs.cpio.gz"
cd "$BUILD_DIR"
find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "${PROJECT_DIR}/build/initramfs.cpio.gz"

echo ""
echo "=== Done ==="
echo "Initramfs: ${PROJECT_DIR}/build/initramfs.cpio.gz"
echo "Size: $(du -h "${PROJECT_DIR}/build/initramfs.cpio.gz" | cut -f1)"
echo ""
echo "Boot with:"
echo "  qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -smp 4 -m 4G \\"
echo "    -kernel kernel/out/Image \\"
echo "    -initrd build/initramfs.cpio.gz \\"
echo "    -drive file=images/system.img,format=raw,if=virtio,readonly=on \\"
echo "    -nographic -no-reboot \\"
echo "    -append 'console=ttyAMA0 root=/dev/vda rw'"
