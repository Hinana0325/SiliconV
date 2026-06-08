#!/bin/bash
# SiliconV — QEMU Test Script
#
# Tests SiliconV kernel with QEMU (ARM64 system emulation).
# Requires: qemu-system-aarch64, aarch64 cross-compiler
#
# Usage: ./scripts/test_qemu.sh [kernel] [rootfs]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

KERNEL="${1:-${PROJECT_DIR}/kernel/out/Image}"
ROOTFS="${2:-${PROJECT_DIR}/build/rootfs.img}"
DTB=""
CMDLINE="console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw"

echo "=== SiliconV QEMU Test ==="
echo "Kernel: ${KERNEL}"
echo "Rootfs: ${ROOTFS}"
echo ""

# Check for QEMU
if ! command -v qemu-system-aarch64 &>/dev/null; then
    echo "ERROR: qemu-system-aarch64 not found"
    echo "Install with: apt install qemu-system-arm"
    exit 1
fi

# Check kernel exists
if [ ! -f "${KERNEL}" ]; then
    echo "ERROR: Kernel not found: ${KERNEL}"
    echo "Run: ./scripts/build_kernel.sh"
    exit 1
fi

# Build DTB if not present
if [ ! -f "${PROJECT_DIR}/build/siliconv.dtb" ]; then
    echo "Generating DTB..."
    # Use dtc to compile DTS (if available)
    if command -v dtc &>/dev/null && [ -f "${PROJECT_DIR}/spec/boot/siliconv.dts" ]; then
        dtc -I dts -O dtb -o "${PROJECT_DIR}/build/siliconv.dtb" \
            "${PROJECT_DIR}/spec/boot/siliconv.dts"
        DTB="${PROJECT_DIR}/build/siliconv.dtb"
    fi
fi

# Build QEMU command
QEMU_ARGS=(
    -machine virt,gic-version=3,highmem=off
    -cpu cortex-a55
    -smp 4
    -m 4G
    -kernel "${KERNEL}"
    -nographic
    -serial mon:stdio
)

# Add DTB if available
if [ -n "${DTB}" ] && [ -f "${DTB}" ]; then
    QEMU_ARGS+=(-dtb "${DTB}")
fi

# Add rootfs if available
if [ -f "${ROOTFS}" ]; then
    QEMU_ARGS+=(
        -drive "file=${ROOTFS},format=raw,if=virtio,readonly=off"
        -append "${CMDLINE}"
    )
else
    echo "WARNING: No rootfs found, booting with initramfs only"
    QEMU_ARGS+=(-append "${CMDLINE}")
fi

# Add network
QEMU_ARGS+=(
    -device virtio-net-device,netdev=net0
    -netdev user,id=net0,hostfwd=tcp::5555-:5555
)

echo "Starting QEMU..."
echo "Press Ctrl-A X to exit"
echo ""

exec qemu-system-aarch64 "${QEMU_ARGS[@]}"
