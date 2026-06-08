#!/bin/bash
# SiliconV — Rootfs Preparation Script
#
# Creates a bootable rootfs image from AOSP GSI components.
# Combines the GSI system.img with SiliconV-specific init files.
#
# Usage: ./scripts/prepare_rootfs.sh [gsi_dir] [output]
#   gsi_dir: directory with GSI images (default: build/gsi)
#   output: output rootfs image (default: build/rootfs.img)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
GSI_DIR="${1:-${PROJECT_DIR}/build/gsi}"
OUTPUT="${2:-${PROJECT_DIR}/build/rootfs.img}"
MOUNT_DIR="${PROJECT_DIR}/build/mount"
ROOTFS_SIZE_MB=4096  # 4GB rootfs

echo "=== SiliconV Rootfs Builder ==="
echo "GSI dir:  ${GSI_DIR}"
echo "Output:   ${OUTPUT}"
echo "Size:     ${ROOTFS_SIZE_MB} MB"
echo ""

# Check for required tools
for cmd in mkfs.ext4 mount umount; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: $cmd not found"
        exit 1
    fi
done

# Check for root (needed for mount)
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script requires root privileges"
    echo "Run with: sudo ./scripts/prepare_rootfs.sh"
    exit 1
fi

# ── Check GSI files ─────────────────────────────

if [ ! -f "${GSI_DIR}/system.img" ]; then
    echo "ERROR: system.img not found in ${GSI_DIR}/"
    echo ""
    echo "Download GSI first:"
    echo "  ./scripts/download_gsi.sh"
    echo ""
    echo "Or manually place system.img in ${GSI_DIR}/"
    exit 1
fi

echo "Found GSI: $(du -h "${GSI_DIR}/system.img" | cut -f1)"

# ── Create rootfs image ─────────────────────────

echo ""
echo "Creating ${ROOTFS_SIZE_MB}MB rootfs image..."

# Create empty image
dd if=/dev/zero of="${OUTPUT}" bs=1M count="${ROOTFS_SIZE_MB}" status=progress

# Format as ext4
mkfs.ext4 -F -L system "${OUTPUT}"

# Mount
mkdir -p "${MOUNT_DIR}"
mount -o loop "${OUTPUT}" "${MOUNT_DIR}"

# ── Extract GSI system ──────────────────────────

echo "Extracting GSI system image..."

# If system.img is an ext4 image, mount it directly
if file "${GSI_DIR}/system.img" | grep -q "ext4"; then
    GSI_MOUNT="${PROJECT_DIR}/build/gsi_mount"
    mkdir -p "${GSI_MOUNT}"
    mount -o loop,ro "${GSI_DIR}/system.img" "${GSI_MOUNT}"
    cp -a "${GSI_MOUNT}/"* "${MOUNT_DIR}/"
    umount "${GSI_MOUNT}"
    rmdir "${GSI_MOUNT}"
elif file "${GSI_DIR}/system.img" | grep -q "sparse"; then
    # Convert sparse image first
    echo "Converting sparse image..."
    simg2img "${GSI_DIR}/system.img" "${PROJECT_DIR}/build/system_raw.img"
    GSI_MOUNT="${PROJECT_DIR}/build/gsi_mount"
    mkdir -p "${GSI_MOUNT}"
    mount -o loop,ro "${PROJECT_DIR}/build/system_raw.img" "${GSI_MOUNT}"
    cp -a "${GSI_MOUNT}/"* "${MOUNT_DIR}/"
    umount "${GSI_MOUNT}"
    rmdir "${GSI_MOUNT}"
    rm -f "${PROJECT_DIR}/build/system_raw.img"
else
    echo "WARNING: Unknown system.img format, attempting direct mount..."
    GSI_MOUNT="${PROJECT_DIR}/build/gsi_mount"
    mkdir -p "${GSI_MOUNT}"
    mount -o loop,ro "${GSI_DIR}/system.img" "${GSI_MOUNT}" 2>/dev/null || true
    if mountpoint -q "${GSI_MOUNT}"; then
        cp -a "${GSI_MOUNT}/"* "${MOUNT_DIR}/"
        umount "${GSI_MOUNT}"
    else
        echo "ERROR: Could not mount system.img"
        umount "${MOUNT_DIR}" 2>/dev/null || true
        rmdir "${MOUNT_DIR}" 2>/dev/null || true
        exit 1
    fi
    rmdir "${GSI_MOUNT}" 2>/dev/null || true
fi

# ── Apply SiliconV-specific files ────────────────

echo "Applying SiliconV configuration..."

# Create vendor directory structure
mkdir -p "${MOUNT_DIR}/vendor/etc/init"
mkdir -p "${MOUNT_DIR}/vendor/lib64"
mkdir -p "${MOUNT_DIR}/vendor/lib/hw"

# Copy init files
cp "${PROJECT_DIR}/android/init/init.siliconv.rc" \
   "${MOUNT_DIR}/vendor/etc/init/"
cp "${PROJECT_DIR}/android/init/init.siliconv.sh" \
   "${MOUNT_DIR}/vendor/etc/init/"
cp "${PROJECT_DIR}/android/init/fstab.siliconv" \
   "${MOUNT_DIR}/vendor/etc/"
chmod 755 "${MOUNT_DIR}/vendor/etc/init/init.siliconv.sh"

# Copy SELinux policy if present
if [ -f "${PROJECT_DIR}/android/sepolicy/siliconv.te" ]; then
    mkdir -p "${MOUNT_DIR}/vendor/etc/selinux"
    cp "${PROJECT_DIR}/android/sepolicy/siliconv.te" \
       "${MOUNT_DIR}/vendor/etc/selinux/"
fi

# Copy HAL shims if present
if [ -d "${PROJECT_DIR}/android/shims" ]; then
    cp "${PROJECT_DIR}/android/shims/"*.h \
       "${MOUNT_DIR}/vendor/lib/hw/" 2>/dev/null || true
fi

# Create build.prop for SiliconV
cat > "${MOUNT_DIR}/vendor/build.prop" << 'EOF'
# SiliconV Virtual Machine
ro.vendor.build.id=SV0
ro.vendor.build.display.id=SiliconV-1.0
ro.vendor.build.type=userdebug
ro.vendor.build.version.sdk=34
ro.product.vendor.model=SiliconV
ro.product.vendor.device=siliconv
ro.product.vendor.manufacturer=SiliconV
ro.hardware=siliconv
ro.hardware.chipname=virtual
ro.hardware.egl=mesa
ro.hardware.vulkan=mesa
ro.sf.lcd_density=320
EOF

# Create default.prop for development
cat > "${MOUNT_DIR}/default.prop" << 'EOF'
# SiliconV Development Properties
ro.debuggable=1
persist.sys.usb.config=adb
ro.adb.secure=0
ro.secure=0
service.adb.root=1
EOF

# ── Create necessary directories ─────────────────

echo "Creating directory structure..."

mkdir -p "${MOUNT_DIR}/dev"
mkdir -p "${MOUNT_DIR}/proc"
mkdir -p "${MOUNT_DIR}/sys"
mkdir -p "${MOUNT_DIR}/mnt"
mkdir -p "${MOUNT_DIR}/tmp"
mkdir -p "${MOUNT_DIR}/data"
mkdir -p "${MOUNT_DIR}/cache"

# ── Unmount and finalize ────────────────────────

echo "Finalizing rootfs..."

sync
umount "${MOUNT_DIR}"
rmdir "${MOUNT_DIR}"

echo ""
echo "=== Rootfs Complete ==="
echo "Image: ${OUTPUT}"
echo "Size:  $(du -h "${OUTPUT}" | cut -f1)"
echo ""
echo "Boot with:"
echo "  ./build/sv-cli -k <kernel> -r ${OUTPUT}"
