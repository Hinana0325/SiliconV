#!/bin/bash
# SiliconV — Android Multi-Partition Image Builder
#
# Builds Android multi-partition image set for QEMU AOSP boot:
#   1. system.img  — from GSI (with sparse→raw conversion)
#   2. vendor.img  — minimal vendor with SiliconV configuration
#   3. userdata.img — 1GB empty ext4
#   4. cache.img   — 256MB empty ext4
#   5. metadata.img — 64MB empty ext4
#
# Usage: ./scripts/build_android_images.sh [--skip-system]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
OUTPUT_DIR="${BUILD_DIR}/android_images"
GSI_DIR="${BUILD_DIR}/gsi"

# Source paths for SiliconV configuration
ANDROID_INIT_DIR="${PROJECT_DIR}/android/init"
ANDROID_SEPOLICY_DIR="${PROJECT_DIR}/android/sepolicy"

# Image sizes
VENDOR_SIZE="128M"
USERDATA_SIZE="1G"
CACHE_SIZE="256M"
METADATA_SIZE="64M"

# Vendor staging directory
VENDOR_STAGING=""

SKIP_SYSTEM=false

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; NC='\033[0m'
pass() { echo -e "  ${GREEN}✓${NC} $1"; }
fail() { echo -e "  ${RED}✗${NC} $1"; }
warn() { echo -e "  ${YELLOW}!${NC} $1"; }

# ── Parse Arguments ───────────────────────────────

for arg in "$@"; do
    case "$arg" in
        --skip-system) SKIP_SYSTEM=true ;;
        --help)
            echo "Usage: $0 [--skip-system]"
            echo ""
            echo "Options:"
            echo "  --skip-system   Skip system.img processing (saves time)"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            exit 1
            ;;
    esac
done

echo "╔══════════════════════════════════════════════════╗"
echo "║     SiliconV Android Image Builder               ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# ── Prerequisites ──────────────────────────────────

echo "━━━ Checking prerequisites ━━━"

if ! command -v mkfs.ext4 &>/dev/null; then
    fail "mkfs.ext4 not found (install: apt install e2fsprogs)"
    exit 1
fi
pass "mkfs.ext4 available"

if [ "${SKIP_SYSTEM}" = false ]; then
    if [ ! -f "${GSI_DIR}/system.img" ]; then
        fail "GSI system.img not found: ${GSI_DIR}/system.img"
        echo ""
        echo "Download AOSP GSI first:"
        echo "  ./scripts/download_gsi.sh"
        echo ""
        echo "Or place system.img in: ${GSI_DIR}/"
        exit 1
    fi
    pass "GSI system.img found ($(du -h "${GSI_DIR}/system.img" | cut -f1))"
fi

echo ""

# ── Prepare Output ────────────────────────────────

mkdir -p "${OUTPUT_DIR}"

# ── Step 1: System Image ──────────────────────────

if [ "${SKIP_SYSTEM}" = false ]; then
    echo "━━━ Step 1/5: Processing system.img ━━━"

    SYSTEM_OUTPUT="${OUTPUT_DIR}/system.img"

    # Detect sparse image by checking magic bytes
    # Android sparse image magic: 0x3AFF26ED
    IS_SPARSE=false
    MAGIC=$(xxd -l 4 -e "${GSI_DIR}/system.img" 2>/dev/null | awk '{print $2}' || true)
    if [ "${MAGIC}" = "ed26ff3a" ]; then
        IS_SPARSE=true
        warn "Detected Android sparse image format"
    fi

    if [ "${IS_SPARSE}" = true ]; then
        if command -v simg2img &>/dev/null; then
            echo "  Converting sparse image to raw..."
            simg2img "${GSI_DIR}/system.img" "${SYSTEM_OUTPUT}"
            pass "Converted to raw: ${SYSTEM_OUTPUT} ($(du -h "${SYSTEM_OUTPUT}" | cut -f1))"
        elif command -v convert_simg2img &>/dev/null; then
            echo "  Converting sparse image to raw (convert_simg2img)..."
            convert_simg2img "${GSI_DIR}/system.img" "${SYSTEM_OUTPUT}"
            pass "Converted to raw: ${SYSTEM_OUTPUT} ($(du -h "${SYSTEM_OUTPUT}" | cut -f1))"
        else
            fail "simg2img not found — cannot convert sparse image"
            echo ""
            echo "Install simg2img:"
            echo "  Debian/Ubuntu: apt install android-sdk-libsparse-utils"
            echo "  AOSP:          m simg2img"
            echo ""
            echo "Or use --skip-system and manually convert:"
            echo "  simg2img ${GSI_DIR}/system.img ${SYSTEM_OUTPUT}"
            exit 1
        fi
    else
        # Raw image — copy directly
        echo "  Copying raw system image..."
        cp "${GSI_DIR}/system.img" "${SYSTEM_OUTPUT}"
        pass "System image copied ($(du -h "${SYSTEM_OUTPUT}" | cut -f1))"
    fi
else
    echo "━━━ Step 1/5: Skipping system.img (--skip-system) ━━━"
fi

echo ""

# ── Step 2: Vendor Image ──────────────────────────

echo "━━━ Step 2/5: Building vendor.img ━━━"

VENDOR_STAGING=$(mktemp -d "${BUILD_DIR}/vendor_staging.XXXXXX")
trap 'rm -rf "${VENDOR_STAGING}"' EXIT

# Create vendor directory structure
mkdir -p "${VENDOR_STAGING}/etc/init"
mkdir -p "${VENDOR_STAGING}/etc/selinux"

# Install SiliconV init configuration
if [ -f "${ANDROID_INIT_DIR}/init.siliconv.rc" ]; then
    cp "${ANDROID_INIT_DIR}/init.siliconv.rc" "${VENDOR_STAGING}/etc/init/"
    pass "Installed /etc/init/init.siliconv.rc"
else
    fail "Missing: ${ANDROID_INIT_DIR}/init.siliconv.rc"
fi

if [ -f "${ANDROID_INIT_DIR}/init.siliconv.sh" ]; then
    cp "${ANDROID_INIT_DIR}/init.siliconv.sh" "${VENDOR_STAGING}/etc/init/"
    chmod 755 "${VENDOR_STAGING}/etc/init/init.siliconv.sh"
    pass "Installed /etc/init/init.siliconv.sh"
else
    fail "Missing: ${ANDROID_INIT_DIR}/init.siliconv.sh"
fi

# Install fstab
if [ -f "${ANDROID_INIT_DIR}/fstab.siliconv" ]; then
    cp "${ANDROID_INIT_DIR}/fstab.siliconv" "${VENDOR_STAGING}/etc/"
    pass "Installed /etc/fstab.siliconv"
else
    fail "Missing: ${ANDROID_INIT_DIR}/fstab.siliconv"
fi

# Install SELinux file_contexts (if exists)
if [ -f "${ANDROID_SEPOLICY_DIR}/file_contexts" ]; then
    cp "${ANDROID_SEPOLICY_DIR}/file_contexts" "${VENDOR_STAGING}/etc/selinux/"
    pass "Installed /etc/selinux/file_contexts"
else
    warn "No file_contexts found in ${ANDROID_SEPOLICY_DIR}/ (skipping)"
fi

# Generate vendor build.prop
cat > "${VENDOR_STAGING}/build.prop" << 'PROP'
# SiliconV Vendor Build Properties
ro.hardware=siliconv
ro.hardware.chipname=virtual
ro.product.board=siliconv
ro.board.platform=siliconv
ro.product.vendor.device=siliconv
ro.product.vendor.model=SiliconV VM
ro.product.vendor.brand=SiliconV
ro.product.vendor.manufacturer=SiliconV
ro.vendor.product.device=siliconv
ro.vendor.product.model=SiliconV VM
ro.vendor.product.brand=SiliconV
ro.vendor.product.manufacturer=SiliconV

# Virtual hardware flags
ro.hardware.virtio=true
ro.kernel.qemu=1
ro.kernel.android.qemud=1

# Disable unavailable hardware
ro.hardware.bluetooth=false
ro.hardware.wifi=false
ro.hardware.camera=false
ro.hardware.gps=false
ro.hardware.nfc=false

# Audio
ro.hardware.audio.primary=stub

# Graphics
ro.hardware.egl=swiftshader
ro.hardware.gralloc=drmhwc
ro.hardware.vulkan=ranchu

# SELinux
ro.build.selinux.enforcing=0
ro.boot.selinux=permissive

# Debug
ro.debuggable=1
ro.adb.secure=0
persist.sys.usb.config=mtp,adb
PROP
pass "Generated /build.prop"

VENDOR_OUTPUT="${OUTPUT_DIR}/vendor.img"

echo "  Creating vendor ext4 image (${VENDOR_SIZE})..."
if mkfs.ext4 -L "vendor" -d "${VENDOR_STAGING}" "${VENDOR_OUTPUT}" "${VENDOR_SIZE}" 2>/dev/null; then
    pass "vendor.img created ($(du -h "${VENDOR_OUTPUT}" | cut -f1))"
else
    fail "Failed to create vendor.img"
    rm -rf "${VENDOR_STAGING}"
    exit 1
fi

# Clean up staging
rm -rf "${VENDOR_STAGING}"
VENDOR_STAGING=""

echo ""

# ── Step 3: Userdata Image ────────────────────────

echo "━━━ Step 3/5: Building userdata.img ━━━"

USERDATA_OUTPUT="${OUTPUT_DIR}/userdata.img"

if mkfs.ext4 -L "userdata" "${USERDATA_OUTPUT}" "${USERDATA_SIZE}" 2>/dev/null; then
    pass "userdata.img created (${USERDATA_SIZE})"
else
    fail "Failed to create userdata.img"
    exit 1
fi

echo ""

# ── Step 4: Cache Image ──────────────────────────

echo "━━━ Step 4/5: Building cache.img ━━━"

CACHE_OUTPUT="${OUTPUT_DIR}/cache.img"

if mkfs.ext4 -L "cache" "${CACHE_OUTPUT}" "${CACHE_SIZE}" 2>/dev/null; then
    pass "cache.img created (${CACHE_SIZE})"
else
    fail "Failed to create cache.img"
    exit 1
fi

echo ""

# ── Step 5: Metadata Image ───────────────────────

echo "━━━ Step 5/5: Building metadata.img ━━━"

METADATA_OUTPUT="${OUTPUT_DIR}/metadata.img"

if mkfs.ext4 -L "metadata" "${METADATA_OUTPUT}" "${METADATA_SIZE}" 2>/dev/null; then
    pass "metadata.img created (${METADATA_SIZE})"
else
    fail "Failed to create metadata.img"
    exit 1
fi

echo ""

# ── Summary ──────────────────────────────────────

echo "╔══════════════════════════════════════════════════╗"
echo "║              Build Summary                       ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""
echo "Output directory: ${OUTPUT_DIR}/"
echo ""

IMAGES_OK=0
IMAGES_TOTAL=5

if [ "${SKIP_SYSTEM}" = false ] && [ -f "${OUTPUT_DIR}/system.img" ]; then
    echo "  system.img    $(du -h "${OUTPUT_DIR}/system.img" | cut -f1)  (GSI)"
    IMAGES_OK=$((IMAGES_OK + 1))
elif [ "${SKIP_SYSTEM}" = true ]; then
    echo "  system.img    (skipped)"
    IMAGES_TOTAL=$((IMAGES_TOTAL - 1))
else
    echo "  system.img    MISSING"
fi

if [ -f "${OUTPUT_DIR}/vendor.img" ]; then
    echo "  vendor.img    $(du -h "${OUTPUT_DIR}/vendor.img" | cut -f1)  (SiliconV)"
    IMAGES_OK=$((IMAGES_OK + 1))
else
    echo "  vendor.img    MISSING"
fi

if [ -f "${OUTPUT_DIR}/userdata.img" ]; then
    echo "  userdata.img  $(du -h "${OUTPUT_DIR}/userdata.img" | cut -f1)"
    IMAGES_OK=$((IMAGES_OK + 1))
else
    echo "  userdata.img  MISSING"
fi

if [ -f "${OUTPUT_DIR}/cache.img" ]; then
    echo "  cache.img     $(du -h "${OUTPUT_DIR}/cache.img" | cut -f1)"
    IMAGES_OK=$((IMAGES_OK + 1))
else
    echo "  cache.img     MISSING"
fi

if [ -f "${OUTPUT_DIR}/metadata.img" ]; then
    echo "  metadata.img  $(du -h "${OUTPUT_DIR}/metadata.img" | cut -f1)"
    IMAGES_OK=$((IMAGES_OK + 1))
else
    echo "  metadata.img  MISSING"
fi

echo ""

if [ "${IMAGES_OK}" -eq "${IMAGES_TOTAL}" ]; then
    pass "All ${IMAGES_TOTAL} images built successfully"
else
    warn "${IMAGES_OK}/${IMAGES_TOTAL} images built"
fi

echo ""
echo "━━━ QEMU Boot Instructions ━━━"
echo ""
echo "  qemu-system-aarch64 \\"
echo "    -M virt,gic-version=3,highmem=off \\"
echo "    -cpu cortex-a55 -smp 4 -m 4G \\"
echo "    -kernel kernel/out/Image \\"
echo "    -drive file=${OUTPUT_DIR}/system.img,format=raw,if=virtio,readonly=on \\"
echo "    -drive file=${OUTPUT_DIR}/vendor.img,format=raw,if=virtio,readonly=on \\"
echo "    -drive file=${OUTPUT_DIR}/userdata.img,format=raw,if=virtio \\"
echo "    -drive file=${OUTPUT_DIR}/cache.img,format=raw,if=virtio \\"
echo "    -drive file=${OUTPUT_DIR}/metadata.img,format=raw,if=virtio \\"
echo "    -device virtio-net-device,netdev=net0 \\"
echo "    -netdev user,id=net0,hostfwd=tcp::5555-:5555 \\"
echo "    -nographic \\"
echo "    -append 'console=ttyAMA0 root=/dev/vda rw'"
echo ""
