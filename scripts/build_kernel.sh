#!/bin/bash
# SiliconV — Android Kernel Build Script
#
# Builds the AOSP Common Kernel with SiliconV's android.config applied.
# Requires: aarch64-linux-gnu-gcc (cross-compiler)
#
# Usage: ./scripts/build_kernel.sh [branch]
#   branch: android14-6.6 (default), android15-6.6, etc.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BRANCH="${1:-android14-6.6}"
BUILD_DIR="${PROJECT_DIR}/kernel/build"
OUT_DIR="${PROJECT_DIR}/kernel/out"
JOBS=$(nproc)

echo "=== SiliconV Kernel Builder ==="
echo "Branch: ${BRANCH}"
echo "Build dir: ${BUILD_DIR}"
echo "Jobs: ${JOBS}"
echo ""

# Check for cross-compiler
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
if ! command -v "${CROSS_COMPILE}gcc" &>/dev/null; then
    echo "ERROR: Cross-compiler not found: ${CROSS_COMPILE}gcc"
    echo "Install with: apt install gcc-aarch64-linux-gnu"
    exit 1
fi

# Clone AOSP common kernel if not present
if [ ! -d "${BUILD_DIR}" ]; then
    echo "Cloning AOSP common kernel (${BRANCH})..."
    mkdir -p "$(dirname "${BUILD_DIR}")"
    git clone --depth=1 --branch "${BRANCH}" \
        https://android.googlesource.com/kernel/common \
        "${BUILD_DIR}"
fi

cd "${BUILD_DIR}"

# Apply SiliconV kernel config
echo "Applying SiliconV android.config..."
cp "${PROJECT_DIR}/kernel/configs/android.config" \
   "${BUILD_DIR}/arch/arm64/configs/siliconv_defconfig"

# Merge with standard defconfig
ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" \
    make ARCH=arm64 gki_defconfig 2>/dev/null || true

# Append SiliconV config
cat "${PROJECT_DIR}/kernel/configs/android.config" >> \
    "${BUILD_DIR}/.config"

# Resolve dependencies
ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" \
    make ARCH=arm64 olddefconfig

echo ""
echo "Building kernel..."
ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" \
    make ARCH=arm64 -j"${JOBS}" Image

# Copy output
mkdir -p "${OUT_DIR}"
cp "${BUILD_DIR}/arch/arm64/boot/Image" "${OUT_DIR}/Image"

echo ""
echo "=== Build Complete ==="
echo "Kernel: ${OUT_DIR}/Image"
echo "Size: $(du -h "${OUT_DIR}/Image" | cut -f1)"
