#!/bin/bash
# SiliconV — Master Build Script
#
# Orchestrates the complete build pipeline:
#   1. Build SiliconV hypervisor
#   2. Build Android kernel (optional, requires ARM64 cross-compiler)
#   3. Prepare rootfs from AOSP GSI
#
# Usage: ./scripts/build_all.sh [--skip-kernel] [--skip-gsi]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

SKIP_KERNEL=false
SKIP_GSI=false

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --skip-kernel) SKIP_KERNEL=true ;;
        --skip-gsi) SKIP_GSI=true ;;
        --help)
            echo "Usage: $0 [--skip-kernel] [--skip-gsi]"
            echo ""
            echo "Options:"
            echo "  --skip-kernel   Skip Android kernel build"
            echo "  --skip-gsi      Skip GSI download/rootfs preparation"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            exit 1
            ;;
    esac
done

echo "╔══════════════════════════════════════════════════╗"
echo "║         SiliconV Master Build System             ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# ── Step 1: Build Hypervisor ─────────────────────

echo "━━━ Step 1/3: Building SiliconV Hypervisor ━━━"
echo ""

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${PROJECT_DIR}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo ""
echo "✓ Hypervisor built: ${BUILD_DIR}/sv-cli"
echo ""

# ── Step 2: Build Android Kernel ─────────────────

if [ "${SKIP_KERNEL}" = false ]; then
    echo "━━━ Step 2/3: Building Android Kernel ━━━"
    echo ""

    if ! command -v aarch64-linux-gnu-gcc &>/dev/null; then
        echo "⚠ Cross-compiler not found, skipping kernel build"
        echo "  Install: apt install gcc-aarch64-linux-gnu"
        echo "  Or run:  ./scripts/build_kernel.sh"
        SKIP_KERNEL=true
    else
        "${SCRIPT_DIR}/build_kernel.sh"
        echo ""
        echo "✓ Kernel built"
    fi
else
    echo "━━━ Step 2/3: Skipping Kernel Build ━━━"
fi
echo ""

# ── Step 3: Prepare Rootfs ───────────────────────

if [ "${SKIP_GSI}" = false ]; then
    echo "━━━ Step 3/3: Preparing Rootfs ━━━"
    echo ""

    if [ ! -f "${BUILD_DIR}/gsi/system.img" ]; then
        echo "⚠ GSI system.img not found"
        echo ""
        echo "Download AOSP GSI:"
        echo "  1. Visit: https://developer.android.com/topic/generic-system-image/releases"
        echo "  2. Download ARM64 + userdebug image"
        echo "  3. Extract to: ${BUILD_DIR}/gsi/"
        echo ""
        echo "Or run: ./scripts/download_gsi.sh"
        echo ""
        echo "Skipping rootfs preparation."
        SKIP_GSI=true
    else
        if [ "$(id -u)" -ne 0 ]; then
            echo "⚠ Rootfs preparation requires root privileges"
            echo "  Run: sudo ./scripts/prepare_rootfs.sh"
            SKIP_GSI=true
        else
            "${SCRIPT_DIR}/prepare_rootfs.sh"
            echo ""
            echo "✓ Rootfs prepared"
        fi
    fi
else
    echo "━━━ Step 3/3: Skipping Rootfs Preparation ━━━"
fi
echo ""

# ── Summary ──────────────────────────────────────

echo "╔══════════════════════════════════════════════════╗"
echo "║              Build Summary                       ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

echo "Hypervisor: ${BUILD_DIR}/sv-cli"
if [ -f "${BUILD_DIR}/sv-cli" ]; then
    echo "  Status: ✓ Built ($(du -h "${BUILD_DIR}/sv-cli" | cut -f1))"
fi

if [ "${SKIP_KERNEL}" = false ] && [ -f "${BUILD_DIR}/../kernel/out/Image" ]; then
    echo ""
    echo "Kernel: ${BUILD_DIR}/../kernel/out/Image"
    echo "  Status: ✓ Built ($(du -h "${BUILD_DIR}/../kernel/out/Image" | cut -f1))"
fi

if [ "${SKIP_GSI}" = false ] && [ -f "${BUILD_DIR}/rootfs.img" ]; then
    echo ""
    echo "Rootfs: ${BUILD_DIR}/rootfs.img"
    echo "  Status: ✓ Built ($(du -h "${BUILD_DIR}/rootfs.img" | cut -f1))"
fi

echo ""
echo "━━━ Run Instructions ━━━"
echo ""
echo "On ARM64 Linux host:"
echo "  ./build/sv-cli -k kernel/out/Image -r build/rootfs.img"
echo ""
echo "With QEMU (for testing):"
echo "  ./scripts/test_qemu.sh"
echo ""
