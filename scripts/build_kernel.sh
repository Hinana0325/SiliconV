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

# ── Clone kernel source ─────────────────────────────
# Try multiple sources (Alibaba Cloud mirrors first for China access):
# 1. mirrors.aliyun.com/android.googlesource.com (AOSP mirror, preferred for China)
# 2. mirrors.aliyun.com/linux-kernel/ (vanilla kernel.org mirror, China)
# 3. android.googlesource.com (official, may be blocked in China)
# 4. cdn.kernel.org (vanilla kernel.org, last resort)

ALIYUN_AOSP_KERNEL="https://mirrors.aliyun.com/android.googlesource.com/kernel/common"
ALIYUN_VANILLA_URL="https://mirrors.aliyun.com/linux-kernel/v6.x/linux-${BRANCH##*.}.tar.xz"
GOOGLE_AOSP_KERNEL="https://android.googlesource.com/kernel/common"
KERNEL_ORG_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${BRANCH##*.}.tar.xz"

if [ ! -d "${BUILD_DIR}/.git" ]; then
    echo "Cloning AOSP common kernel (${BRANCH})..."
    mkdir -p "$(dirname "${BUILD_DIR}")"

    CLONE_OK=false

    # Method 1: Alibaba Cloud AOSP mirror (git clone)
    echo "  [1/4] Trying Alibaba Cloud AOSP mirror (git)..."
    if git clone --depth=1 --branch "${BRANCH}" \
        "${ALIYUN_AOSP_KERNEL}" \
        "${BUILD_DIR}" 2>/dev/null; then
        echo "  ✓ Cloned from mirrors.aliyun.com/android.googlesource.com"
        CLONE_OK=true
    else
        echo "  ✗ Alibaba Cloud AOSP mirror unreachable"
    fi

    # Method 2: Alibaba Cloud vanilla kernel mirror (tarball, fast in China)
    if [ "${CLONE_OK}" = false ]; then
        echo "  [2/4] Trying Alibaba Cloud vanilla kernel mirror..."
        if curl -fsSL --connect-timeout 15 -o /tmp/linux-src.tar.xz "${ALIYUN_VANILLA_URL}" 2>/dev/null && \
           [ -s /tmp/linux-src.tar.xz ]; then
            echo "  ✓ Downloaded Linux ${BRANCH##*.} from mirrors.aliyun.com/linux-kernel"
            mkdir -p "${BUILD_DIR}"
            tar -xJf /tmp/linux-src.tar.xz -C "${BUILD_DIR}" --strip-components=1
            rm -f /tmp/linux-src.tar.xz
            echo "  WARNING: Using vanilla Linux — lacks Android-specific patches (binder etc.)"
            CLONE_OK=true
        else
            echo "  ✗ Alibaba Cloud vanilla kernel mirror failed"
        fi
    fi

    # Method 3: Official android.googlesource.com (git)
    if [ "${CLONE_OK}" = false ]; then
        echo "  [3/4] Trying official AOSP source (git)..."
        if git clone --depth=1 --branch "${BRANCH}" \
            "${GOOGLE_AOSP_KERNEL}" \
            "${BUILD_DIR}" 2>/dev/null; then
            echo "  ✓ Cloned from android.googlesource.com"
            CLONE_OK=true
        else
            echo "  ✗ android.googlesource.com unreachable"
        fi
    fi

    # Method 4: kernel.org vanilla tarball (last resort)
    if [ "${CLONE_OK}" = false ]; then
        echo "  [4/4] Trying kernel.org stable (last resort)..."
        if curl -fsSL --connect-timeout 15 -o /tmp/linux-src.tar.xz "${KERNEL_ORG_URL}" 2>/dev/null && \
           [ -s /tmp/linux-src.tar.xz ]; then
            echo "  ✓ Downloaded Linux ${BRANCH##*.} from kernel.org"
            mkdir -p "${BUILD_DIR}"
            tar -xJf /tmp/linux-src.tar.xz -C "${BUILD_DIR}" --strip-components=1
            rm -f /tmp/linux-src.tar.xz
            echo "  WARNING: Using vanilla Linux — lacks Android-specific patches"
            CLONE_OK=true
        else
            echo "  ✗ kernel.org unreachable"
        fi
    fi

    if [ "${CLONE_OK}" = false ]; then
        echo ""
        echo "ERROR: Cannot download kernel source from any mirror."
        echo ""
        echo "Manual options:"
        echo "  1. Alibaba Cloud: wget ${ALIYUN_VANILLA_URL}"
        echo "  2. kernel.org:    wget ${KERNEL_ORG_URL}"
        echo "  3. Place extracted source in: ${BUILD_DIR}"
        exit 1
    fi
fi

cd "${BUILD_DIR}"

# ── Apply SiliconV kernel config ────────────────────
echo "Applying SiliconV android.config..."

# Start from defconfig (or gki_defconfig if available)
if [ -f "arch/arm64/configs/gki_defconfig" ]; then
    ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" \
        make ARCH=arm64 gki_defconfig 2>/dev/null || true
else
    ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" \
        make ARCH=arm64 defconfig
fi

# Append SiliconV config (forces built-in for DM/verity/crypto)
cat "${PROJECT_DIR}/kernel/configs/android.config" >> \
    "${BUILD_DIR}/.config"

# Resolve dependencies — this may change =y to =m for some options
# if dependencies aren't met, so we verify afterwards
ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" \
    make ARCH=arm64 olddefconfig

# ── Verify critical configs are built-in ────────────
echo ""
echo "Verifying critical kernel configs..."

CRITICAL_CONFIGS=(
    "CONFIG_BLK_DEV_DM"
    "CONFIG_DM_VERITY"
    "CONFIG_ANDROID_BINDER_IPC"
    "CONFIG_EXT4_FS"
    "CONFIG_VIRTIO_BLK"
    "CONFIG_VIRTIO_NET"
    "CONFIG_VIRTIO_MMIO"
    "CONFIG_SECURITY_SELINUX"
)

CONFIG_OK=true
for cfg in "${CRITICAL_CONFIGS[@]}"; do
    VALUE=$(grep "^${cfg}=" .config 2>/dev/null || true)
    if [ -z "${VALUE}" ]; then
        echo "  ✗ ${cfg}: NOT SET"
        CONFIG_OK=false
    elif echo "${VALUE}" | grep -q "=y$"; then
        echo "  ✓ ${cfg}=y (built-in)"
    elif echo "${VALUE}" | grep -q "=m$"; then
        echo "  ⚠ ${cfg}=m (module — should be =y for Android)"
        CONFIG_OK=false
    else
        echo "  ? ${VALUE}"
    fi
done

if [ "${CONFIG_OK}" = false ]; then
    echo ""
    echo "WARNING: Some critical configs are not built-in (=y)."
    echo "Android FirstStageMount may fail. Consider forcing these configs."
    echo ""
fi

# ── Build kernel ────────────────────────────────────
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
