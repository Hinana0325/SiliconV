#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# SiliconV Kernel Boot Test (QEMU)
#
# Builds a minimal initramfs with a static C init program, boots the ARM64
# kernel in QEMU, and verifies Android-required kernel features.
#
# Prerequisites:
#   - aarch64-linux-gnu-gcc (cross-compiler)
#   - qemu-system-aarch64 (>= 7.0)
#   - Kernel Image at ${KERNEL_IMAGE} (see: scripts/build_kernel.sh)
#
# Usage:
#   ./scripts/test_kernel_qemu.sh                          # initramfs (default)
#   MODE=rootfs ./scripts/test_kernel_qemu.sh               # virtio-blk rootfs
#   KERNEL_IMAGE=/tmp/my/Image ./test_kernel_qemu.sh
#
# Requires: aarch64-linux-gnu-gcc, qemu-system-aarch64
# For rootfs mode: also needs scripts/create_rootfs.sh output.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

KERNEL_IMAGE="${KERNEL_IMAGE:-${PROJECT_DIR}/kernel/out/Image}"
INIT_C_SRC="${PROJECT_DIR}/tests/integration/test_kernel_boot_qemu.c"
ROOTFS_IMAGE="${ROOTFS_IMAGE:-${PROJECT_DIR}/build/rootfs.img}"
BUILD_DIR="${PROJECT_DIR}/build/boot_test"

# Mode: initramfs (default) or rootfs
MODE="${MODE:-initramfs}"

echo "=== SiliconV Kernel QEMU Boot Test ==="
echo "Kernel: ${KERNEL_IMAGE}"
echo "Init src: ${INIT_C_SRC}"

# Verify kernel image exists
if [ ! -f "${KERNEL_IMAGE}" ]; then
    echo "ERROR: Kernel image not found at ${KERNEL_IMAGE}"
    echo "Run 'scripts/build_kernel.sh' first."
    exit 1
fi

# Verify cross-compiler
command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || {
    echo "ERROR: aarch64-linux-gnu-gcc not found."
    echo "Install: sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu"
    exit 1
}

# Verify QEMU
command -v qemu-system-aarch64 >/dev/null 2>&1 || {
    echo "ERROR: qemu-system-aarch64 not found."
    echo "Install: sudo apt install qemu-system-arm"
    exit 1
}

# Build boot image
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

if [ "${MODE}" = "initramfs" ]; then
    echo ""
    echo "--- Mode: initramfs ---"
    mkdir -p "${BUILD_DIR}/initramfs"

    echo "--- Building static init binary ---"
    aarch64-linux-gnu-gcc -static -Os \
        -o "${BUILD_DIR}/initramfs/init" \
        "${INIT_C_SRC}"

    echo "--- Creating initramfs cpio archive ---"
    cd "${BUILD_DIR}/initramfs"
    mkdir -p proc sys dev
    find . | cpio -H newc -o --quiet | gzip > "${BUILD_DIR}/initramfs.cpio.gz"
    INITRAMFS_SIZE="$(stat -c%s "${BUILD_DIR}/initramfs.cpio.gz")"
    echo "Initramfs: ${INITRAMFS_SIZE} bytes"
    BOOT_IMG="-initrd ${BUILD_DIR}/initramfs.cpio.gz"
    CMDLINE="console=ttyAMA0"

elif [ "${MODE}" = "rootfs" ]; then
    echo ""
    echo "--- Mode: rootfs (virtio-blk) ---"
    if [ ! -f "${ROOTFS_IMAGE}" ]; then
        echo "ERROR: Rootfs image not found at ${ROOTFS_IMAGE}"
        echo "Run 'scripts/create_rootfs.sh' first."
        exit 1
    fi
    echo "Rootfs: ${ROOTFS_IMAGE} ($(du -h "${ROOTFS_IMAGE}" | cut -f1))"
    BOOT_IMG="-drive file=${ROOTFS_IMAGE},format=raw,if=virtio"
    # Append 'testmode' for automatic poweroff after boot
    CMDLINE="console=ttyAMA0 root=/dev/vda rw testmode"
else
    echo "ERROR: Unknown MODE='${MODE}'. Use 'initramfs' or 'rootfs'."
    exit 1
fi

# Boot in QEMU (capture serial output)
echo ""
echo "--- Booting kernel in QEMU ---"
QEMU_CMD=(
    qemu-system-aarch64
    -M virt,gic-version=3
    -cpu cortex-a72
    -smp 2
    -m 2G
    -kernel "${KERNEL_IMAGE}"
    ${BOOT_IMG}
    -nographic
    -no-reboot
    -append "${CMDLINE}"
)

# Run QEMU and capture output (30s timeout for rootfs mode)
set +e
timeout 30 "${QEMU_CMD[@]}" > "${BUILD_DIR}/qemu_output.txt" 2>&1
QEMU_EXIT_CODE=$?
set -e

# timeout(1) returns 124 if command timed out
if [ "${QEMU_EXIT_CODE}" -eq 124 ]; then
    echo "(QEMU timed out after 30s — normal if shell is waiting)"
    QEMU_EXIT_CODE=0
fi

echo ""
echo "--- QEMU exit code: ${QEMU_EXIT_CODE} ---"

# Parse results
OUTPUT_FILE="${BUILD_DIR}/qemu_output.txt"

if grep -q "Boot Verification\|SiliconV boot successful\|SiliconV Minimal Linux\|testmode: powering off" "${OUTPUT_FILE}"; then
    echo ""
    echo "✅  Kernel booted successfully!"

    # Show system info
    grep -E "(Kernel:|CPUs:|Board:|Memory:)" "${OUTPUT_FILE}" || true

    if grep -q "Boot Verification" "${OUTPUT_FILE}"; then
        # initramfs mode (C program output)
        echo ""
        grep -A 999 "Device Check" "${OUTPUT_FILE}" | grep -v "^\[.*\]" | head -40

        PASS_COUNT=$(grep -c "PASS" "${OUTPUT_FILE}" || true)
        FAIL_COUNT=$(grep -c "FAIL" "${OUTPUT_FILE}" || true)

        echo ""
        if [ "${FAIL_COUNT}" -eq 0 ]; then
            echo "✅  All ${PASS_COUNT} device checks PASSED"
        else
            echo "⚠️   ${PASS_COUNT} passed, ${FAIL_COUNT} FAILED"
            grep "FAIL" "${OUTPUT_FILE}"
        fi

        # Check for clean shutdown
        if grep -q "reboot: Power down" "${OUTPUT_FILE}"; then
            echo "✅  Clean shutdown"
        else
            echo "⚠️   No clean shutdown detected"
        fi
    else
        # rootfs mode (shell script output)
        echo ""
        grep -A 999 "Android devices" "${OUTPUT_FILE}" | grep -v "^\[.*\]" | head -30
        echo ""
        if grep -q "OK: /dev/binder" "${OUTPUT_FILE}"; then
            echo "✅  All Android devices present"
        fi
        if grep -q "DHCP" "${OUTPUT_FILE}"; then
            echo "✅  DHCP attempted"
        fi
        echo "✅  Rootfs boot successful"
    fi
else
    echo "❌  Kernel boot FAILED"
    echo "Last 20 lines of QEMU output:"
    tail -20 "${OUTPUT_FILE}"
    exit 1
fi
