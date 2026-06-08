#!/bin/bash
# SiliconV — Minimal Test Rootfs Creator
#
# Creates a minimal rootfs for testing kernel boot without full AOSP.
# Includes busybox, basic init, and SiliconV device nodes.
#
# Usage: ./scripts/create_minimal_rootfs.sh [output] [size_mb]
#   output: output image (default: build/rootfs-minimal.img)
#   size_mb: image size in MB (default: 512)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT="${1:-${PROJECT_DIR}/build/rootfs-minimal.img}"
SIZE_MB="${2:-512}"
ROOTFS_DIR="${PROJECT_DIR}/build/rootfs_staging"

echo "=== SiliconV Minimal Rootfs Builder ==="
echo "Output: ${OUTPUT}"
echo "Size:   ${SIZE_MB} MB"
echo ""

# Check tools
for cmd in mkfs.ext4 mount umount; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: $cmd not found"
        exit 1
    fi
done

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Requires root. Run: sudo $0 $@"
    exit 1
fi

# Clean staging
rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"

# ── Create directory structure ───────────────────

echo "Creating directory structure..."

mkdir -p "${ROOTFS_DIR}"/{bin,sbin,usr/bin,usr/sbin,lib,lib64}
mkdir -p "${ROOTFS_DIR}"/{dev,proc,sys,mnt,tmp,root}
mkdir -p "${ROOTFS_DIR}"/{etc,var/run,var/log}
mkdir -p "${ROOTFS_DIR}"/{system/vendor,system/app,system/lib,system/lib64}
mkdir -p "${ROOTFS_DIR}"/{data,cache,metadata}

# ── Install busybox (static) ────────────────────

echo "Installing busybox..."

BUSYBOX_URL="https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox"
BUSYBOX="${ROOTFS_DIR}/bin/busybox"

# Try to download busybox
if command -v wget &>/dev/null; then
    wget -q -O "${BUSYBOX}" "${BUSYBOX_URL}" 2>/dev/null || \
    wget -q -O "${BUSYBOX}" "https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox" 2>/dev/null || true
fi

# If download failed, try apt
if [ ! -f "${BUSYBOX}" ] || [ ! -s "${BUSYBOX}" ]; then
    if command -v busybox &>/dev/null; then
        cp "$(command -v busybox)" "${BUSYBOX}"
    else
        echo "WARNING: busybox not available, creating stub"
        # Create a minimal shell script as placeholder
        cat > "${ROOTFS_DIR}/bin/sh" << 'SHEOF'
#!/bin/sh
echo "SiliconV Minimal Shell"
exec /bin/sh
SHEOF
        chmod 755 "${ROOTFS_DIR}/bin/sh"
    fi
fi

if [ -f "${BUSYBOX}" ] && [ -s "${BUSYBOX}" ]; then
    chmod 755 "${BUSYBOX}"

    # Create symlinks for common utilities
    for cmd in sh ash bash cat ls mkdir mount umount echo printf \
               grep sed awk sort uniq wc head tail tr cut \
               cp mv rm ln chmod chown mknod \
               ps kill sleep true false test expr \
               hostname uname dmesg \
               ifconfig route ip ping \
               vi more less clear reset \
               init poweroff reboot halt; do
        ln -sf busybox "${ROOTFS_DIR}/bin/${cmd}" 2>/dev/null || true
    done

    for cmd in init getty sulogin switch_root \
               mke2fs fsck mkswap swapon swapoff \
               blkid fdisk sfdisk \
               wget ftpd telnetd httpd; do
        ln -sf ../bin/busybox "${ROOTFS_DIR}/sbin/${cmd}" 2>/dev/null || true
    done
fi

# ── Create /init ─────────────────────────────────

echo "Creating init script..."

cat > "${ROOTFS_DIR}/init" << 'INITEOF'
#!/bin/sh
# SiliconV Minimal Init

export PATH=/bin:/sbin:/usr/bin:/usr/sbin

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║        SiliconV Minimal Linux            ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Mount virtual filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t tmpfs tmpfs /tmp

# Create device nodes
mknod /dev/console c 5 1 2>/dev/null || true
mknod /dev/null c 1 3 2>/dev/null || true
mknod /dev/zero c 1 5 2>/dev/null || true
mknod /dev/random c 1 8 2>/dev/null || true
mknod /dev/urandom c 1 9 2>/dev/null || true

# Display system info
echo "System: $(uname -a)"
echo "CPU:    $(cat /proc/cpuinfo 2>/dev/null | grep 'model name' | head -1 | cut -d: -f2 || echo 'ARM64')"
echo "Memory: $(cat /proc/meminfo 2>/dev/null | head -1 || echo 'N/A')"
echo ""

# Show virtio devices
echo "Virtio devices:"
ls -la /sys/bus/virtio/devices/ 2>/dev/null || echo "  (none found)"
echo ""

# Network
if [ -d /sys/class/net/eth0 ]; then
    echo "Network: eth0 found"
    ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up 2>/dev/null || true
    route add default gw 10.0.2.2 2>/dev/null || true
    echo "  IP: 10.0.2.15"
fi

echo ""
echo "SiliconV boot successful!"
echo ""

# Start shell
exec /bin/sh
INITEOF

chmod 755 "${ROOTFS_DIR}/init"

# ── Create /etc files ────────────────────────────

echo "Creating /etc files..."

cat > "${ROOTFS_DIR}/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/sh
shell:x:2000:2000:shell:/home/shell:/bin/sh
EOF

cat > "${ROOTFS_DIR}/etc/group" << 'EOF'
root:x:0:
shell:x:2000:
EOF

cat > "${ROOTFS_DIR}/etc/fstab" << 'EOF'
proc    /proc    proc    defaults    0    0
sysfs   /sys     sysfs   defaults    0    0
tmpfs   /tmp     tmpfs   defaults    0    0
EOF

cat > "${ROOTFS_DIR}/etc/hostname" << 'EOF'
siliconv
EOF

# ── Create rootfs image ─────────────────────────

echo ""
echo "Creating ${SIZE_MB}MB rootfs image..."

dd if=/dev/zero of="${OUTPUT}" bs=1M count="${SIZE_MB}" status=none
mkfs.ext4 -F -L rootfs "${OUTPUT}" > /dev/null 2>&1

MOUNT_DIR="${PROJECT_DIR}/build/rootfs_mount"
mkdir -p "${MOUNT_DIR}"
mount -o loop "${OUTPUT}" "${MOUNT_DIR}"

cp -a "${ROOTFS_DIR}/"* "${MOUNT_DIR}/"

sync
umount "${MOUNT_DIR}"
rmdir "${MOUNT_DIR}"

# Cleanup
rm -rf "${ROOTFS_DIR}"

echo ""
echo "=== Minimal Rootfs Complete ==="
echo "Image: ${OUTPUT}"
echo "Size:  $(du -h "${OUTPUT}" | cut -f1)"
echo ""
echo "Test with:"
echo "  qemu-system-aarch64 -machine virt -cpu cortex-a55 \\"
echo "    -kernel <kernel> -drive file=${OUTPUT},if=virtio \\"
echo "    -nographic -append 'console=ttyAMA0 root=/dev/vda rw'"
