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
# Try multiple sources for the AOSP common kernel:
# 1. android.googlesource.com (official, may be blocked)
# 2. Tarball mirror (fallback)
# 3. kernel.org stable Linux (last resort, lacks Android patches)

if [ ! -d "${BUILD_DIR}/.git" ]; then
    echo "Cloning AOSP common kernel (${BRANCH})..."
    mkdir -p "$(dirname "${BUILD_DIR}")"

    # Try official AOSP source first
    if git clone --depth=1 --branch "${BRANCH}" \
        https://android.googlesource.com/kernel/common \
        "${BUILD_DIR}" 2>/dev/null; then
        echo "  ✓ Cloned from android.googlesource.com"
    else
        echo "  ✗ android.googlesource.com unreachable, trying tarball..."

        # Try tarball download
        TARBALL_URL="https://android.googlesource.com/kernel/common/+archive/refs/heads/${BRANCH}.tar.gz"
        mkdir -p "${BUILD_DIR}"

        if curl -fsSL --connect-timeout 10 -o /tmp/kernel-src.tar.gz "${TARBALL_URL}" 2>/dev/null && \
           [ -s /tmp/kernel-src.tar.gz ]; then
            echo "  ✓ Downloaded tarball from android.googlesource.com"
            tar -xzf /tmp/kernel-src.tar.gz -C "${BUILD_DIR}"
            rm -f /tmp/kernel-src.tar.gz
        else
            echo "  ✗ Tarball download failed, trying kernel.org stable..."

            # Fallback: use vanilla Linux 6.6 from kernel.org
            # This lacks Android-specific patches (binder, etc.) but has DM_VERITY
            LINUX_VERSION="6.6"
            STABLE_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VERSION}.tar.xz"

            if curl -fsSL --connect-timeout 10 -o /tmp/linux-src.tar.xz "${STABLE_URL}" 2>/dev/null && \
               [ -s /tmp/linux-src.tar.xz ]; then
                echo "  ✓ Downloaded Linux ${LINUX_VERSION} from kernel.org"
                tar -xJf /tmp/linux-src.tar.xz -C "${BUILD_DIR}" --strip-components=1
                rm -f /tmp/linux-src.tar.xz
                echo "  WARNING: Using vanilla Linux ${LINUX_VERSION} — lacks Android patches"
            else
                echo "ERROR: Cannot download kernel source from any mirror"
                echo "Please manually clone or download kernel source to: ${BUILD_DIR}"
                exit 1
            fi
        fi
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
