#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# SiliconV Minimal ARM64 Rootfs Creator
#
# Builds a minimal ARM64 Linux rootfs with busybox for kernel boot testing.
# Output: an ext4 image bootable via virtio-blk.
#
# Usage:
#   sudo ./scripts/create_rootfs.sh                    # write to build/rootfs.img
#   OUTPUT=/tmp/myrootfs.img ./scripts/create_rootfs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT="${OUTPUT:-${PROJECT_DIR}/build/rootfs.img}"
BUSYBOX_DIR="/tmp/busybox-siliconv"
BUSYBOX_SRC="${BUSYBOX_DIR}/src"
BUSYBOX_CONFIG="${BUSYBOX_DIR}/config"
STAGING="${BUSYBOX_DIR}/staging"
BUSYBOX_VERSION="1.37.0"

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
pass() { echo -e "  ${GREEN}✓${NC} $1"; }
fail() { echo -e "  ${RED}✗${NC} $1"; }

echo "=== SiliconV Minimal ARM64 Rootfs Creator ==="
echo "Output: ${OUTPUT}"
echo ""

# ── Prerequisites ──────────────────────────────────

command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || { fail "aarch64-linux-gnu-gcc not found"; exit 1; }
command -v mkfs.ext4 >/dev/null 2>&1 || { fail "mkfs.ext4 not found"; exit 1; }

# ── Download busybox source ─────────────────────────

if [ ! -f "${BUSYBOX_SRC}/Makefile" ]; then
    echo "Downloading busybox source..."

    mkdir -p "${BUSYBOX_SRC}"
    DOWNLOAD_OK=false

    # Try Alibaba Cloud Debian mirror first (fast in China)
    ALIYUN_BUSYBOX_URL="https://mirrors.aliyun.com/debian/pool/main/b/busybox/busybox_${BUSYBOX_VERSION}.orig.tar.bz2"
    echo "  Trying Alibaba Cloud Debian mirror..."
    if wget -q -O /tmp/busybox.tar.bz2 --timeout=30 "${ALIYUN_BUSYBOX_URL}" 2>/dev/null && \
       [ -s /tmp/busybox.tar.bz2 ]; then
        echo "  ✓ Downloaded from mirrors.aliyun.com/debian"
        DOWNLOAD_OK=true
    else
        echo "  ✗ Alibaba Cloud mirror failed, trying deb.debian.org..."
        if wget -q -O /tmp/busybox.tar.bz2 --timeout=30 \
            "http://deb.debian.org/debian/pool/main/b/busybox/busybox_${BUSYBOX_VERSION}.orig.tar.bz2" 2>/dev/null && \
           [ -s /tmp/busybox.tar.bz2 ]; then
            echo "  ✓ Downloaded from deb.debian.org"
            DOWNLOAD_OK=true
        fi
    fi

    if [ "${DOWNLOAD_OK}" = false ]; then
        fail "Failed to download busybox source from any mirror"
        echo "  Manual: wget ${ALIYUN_BUSYBOX_URL}"
        exit 1
    fi

    tar -xf /tmp/busybox.tar.bz2 -C "${BUSYBOX_SRC}" --strip-components=1 2>/dev/null || {
        tar -xf /tmp/busybox.tar.bz2 -C /tmp/ 2>/dev/null || true
        mv "/tmp/busybox-${BUSYBOX_VERSION}"/* "${BUSYBOX_SRC}/" 2>/dev/null || true
    }
    rm -f /tmp/busybox.tar.bz2
    pass "busybox source downloaded"
else
    pass "busybox source already cached"
fi

# ── Build busybox ───────────────────────────────────

BUSYBOX_BIN="${BUSYBOX_DIR}/busybox"
if [ ! -f "${BUSYBOX_BIN}" ]; then
    echo "Building busybox (ARM64 static)..."
    cd "${BUSYBOX_SRC}"

    # Generate minimal config
    cat > .config << 'CONFEOF'
CONFIG_HAVE_DOT_CONFIG=y
CONFIG_CROSS_COMPILER_PREFIX="aarch64-linux-gnu-"
CONFIG_STATIC=y
CONFIG_PLATFORM_LINUX=y
CONFIG_ASH=y
CONFIG_ASH_JOB_CONTROL=y
CONFIG_ASH_ALIAS=y
CONFIG_ASH_BASH_COMPAT=y
CONFIG_ASH_OPTIMIZE_FOR_SIZE=y
CONFIG_SH_IS_ASH=y
CONFIG_BASH_IS_ASH=y
CONFIG_AWK=y
CONFIG_BASENAME=y
CONFIG_CAT=y
CONFIG_CHMOD=y
CONFIG_CHOWN=y
CONFIG_CP=y
CONFIG_DATE=y
CONFIG_DD=y
CONFIG_DF=y
CONFIG_DIRNAME=y
CONFIG_DMESG=y
CONFIG_ECHO=y
CONFIG_EXPR=y
CONFIG_FALSE=y
CONFIG_FDISK=y
CONFIG_FIND=y
CONFIG_GREP=y
CONFIG_HEAD=y
CONFIG_HOSTNAME=y
CONFIG_ID=y
CONFIG_IFCONFIG=y
CONFIG_INIT=y
CONFIG_KILL=y
CONFIG_LN=y
CONFIG_LS=y
CONFIG_MD5SUM=y
CONFIG_MKDIR=y
CONFIG_MKNOD=y
CONFIG_MKSWAP=y
CONFIG_MKTEMP=y
CONFIG_MORE=y
CONFIG_MOUNT=y
CONFIG_MV=y
CONFIG_NC=y
CONFIG_NETSTAT=y
CONFIG_NICE=y
CONFIG_NPROC=y
CONFIG_PASSWD=y
CONFIG_PGREP=y
CONFIG_PIDOF=y
CONFIG_PING=y
CONFIG_POWEROFF=y
CONFIG_PRINTF=y
CONFIG_PS=y
CONFIG_PWD=y
CONFIG_REBOOT=y
CONFIG_RM=y
CONFIG_RMDIR=y
CONFIG_ROUTE=y
CONFIG_SED=y
CONFIG_SEQ=y
CONFIG_SLEEP=y
CONFIG_SORT=y
CONFIG_STAT=y
CONFIG_SULOGIN=y
CONFIG_SWAPOFF=y
CONFIG_SWAPON=y
CONFIG_SWITCH_ROOT=y
CONFIG_SYNC=y
CONFIG_SYSLOGD=y
CONFIG_TAIL=y
CONFIG_TAR=y
CONFIG_TEE=y
CONFIG_TEST=y
CONFIG_TOUCH=y
CONFIG_TR=y
CONFIG_TRUE=y
CONFIG_TTY=y
CONFIG_UDHCPC=y
CONFIG_UDHCPD=y
CONFIG_UMOUNT=y
CONFIG_UNAME=y
CONFIG_UNIQ=y
CONFIG_UPTIME=y
CONFIG_WATCH=y
CONFIG_WC=y
CONFIG_WGET=y
CONFIG_WHICH=y
CONFIG_WHOAMI=y
CONFIG_XARGS=y
CONFIG_YES=y
CONFEOF

    # Disable problematic modules
    echo "# CONFIG_TC is not set" >> .config
    echo "# CONFIG_FEATURE_TC_INGRESS is not set" >> .config
    echo "# CONFIG_SHA1_HWACCEL is not set" >> .config
    echo "# CONFIG_SHA256_HWACCEL is not set" >> .config

    yes "1" | make oldconfig 2>/dev/null
    make -j$(nproc) 2>&1 | tail -5

    cp busybox "${BUSYBOX_BIN}"
    pass "busybox ARM64 binary built ($(stat -c%s "${BUSYBOX_BIN}") bytes)"
else
    pass "busybox already built"
fi

# ── Create rootfs staging ──────────────────────────

echo "Creating rootfs directory structure..."
rm -rf "${STAGING}"
mkdir -p "${STAGING}"/{bin,sbin,usr/bin,usr/sbin,lib,lib64}
mkdir -p "${STAGING}"/{dev,proc,sys,mnt,tmp,root}
mkdir -p "${STAGING}"/{etc,var/run,var/log}
mkdir -p "${STAGING}"/{data,cache,metadata}

# Install busybox
cp "${BUSYBOX_BIN}" "${STAGING}/bin/busybox"
chmod 755 "${STAGING}/bin/busybox"

# Create applet symlinks in /bin
cd "${STAGING}/bin"
for cmd in sh ash awk basename cat chmod chown cp date dd df dirname \
           dmesg echo expr false find grep head hostname id kill ln ls \
           md5sum mkdir mknod mktemp more mount mv nc netstat nice nproc \
           pidof ping poweroff printf ps pwd reboot rm rmdir sed seq sleep \
           sort stat sync tar tee test touch tr true tty \
           umount uname uniq uptime watch wc wget which whoami xargs yes; do
    ln -sf busybox "$cmd" 2>/dev/null || true
done

# Create /sbin symlinks (NOT init - handled separately)
cd "${STAGING}/sbin"
for cmd in poweroff reboot halt getty sulogin switch_root syslogd \
           udhcpc udhcpd; do
    ln -sf ../bin/busybox "$cmd" 2>/dev/null || true
done
# /sbin/init -> /init (our custom script)
ln -sf ../init init

# ── Configuration files ────────────────────────────

echo "Creating config files..."

# passwd
cat > "${STAGING}/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/sh
shell:x:2000:2000:shell:/data:/bin/sh
EOF
chmod 644 "${STAGING}/etc/passwd"

# group
cat > "${STAGING}/etc/group" << 'EOF'
root:x:0:
shell:x:2000:
EOF
chmod 644 "${STAGING}/etc/group"

# hostname
echo "siliconv" > "${STAGING}/etc/hostname"

# fstab
cat > "${STAGING}/etc/fstab" << 'EOF'
proc    /proc    proc    defaults    0    0
sysfs   /sys     sysfs   defaults    0    0
tmpfs   /tmp     tmpfs   defaults    0    0
EOF

# network interfaces
mkdir -p "${STAGING}/etc/network"
cat > "${STAGING}/etc/network/interfaces" << 'EOF'
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
EOF

# udhcpc default script
mkdir -p "${STAGING}/usr/share/udhcpc"
cat > "${STAGING}/usr/share/udhcpc/default.script" << 'SCRIPT'
#!/bin/busybox sh
# udhcpc default script
case "$1" in
    bound|renew)
        /bin/busybox ifconfig $interface ${ip} netmask ${subnet} ${mtu:+mtu $mtu}
        /bin/busybox route add default gw ${router} 2>/dev/null || true
        echo "Network: ${interface} up, IP ${ip}"
        ;;
    deconfig)
        /bin/busybox ifconfig $interface 0.0.0.0
        ;;
esac
SCRIPT
chmod 755 "${STAGING}/usr/share/udhcpc/default.script"

# ── /init script ──────────────────────────────────

cat > "${STAGING}/init" << 'INITEOF'
#!/bin/busybox sh
# SiliconV Minimal Linux Init

export PATH=/bin:/sbin:/usr/bin:/usr/sbin

# Mount virtual filesystems FIRST
/bin/busybox mount -t proc proc /proc 2>/dev/null
/bin/busybox mount -t sysfs sysfs /sys 2>/dev/null
/bin/busybox mount -t devtmpfs devtmpfs /dev 2>/dev/null
/bin/busybox mount -t tmpfs tmpfs /tmp 2>/dev/null

# NOW read cmdline (proc is mounted)
CMDLINE=$(cat /proc/cmdline 2>/dev/null || echo "")

echo ""
echo "=========================================="
echo "   SiliconV Minimal Linux v6.6"
echo "=========================================="
echo ""
echo "Kernel: $(cat /proc/version | head -1)"
echo "CPUs:   $(grep -c processor /proc/cpuinfo)"
machine=$(cat /proc/device-tree/model 2>/dev/null || echo "unknown")
mem=$(grep MemTotal /proc/meminfo 2>/dev/null | grep -o '[0-9]*' || echo "?")
echo "Board:  ${machine} (${mem} kB RAM)"
echo ""

echo "--- Android devices ---"
for dev in /dev/binder /dev/hwbinder /dev/vndbinder /dev/dma_heap /dev/userfaultfd; do
    if [ -e "$dev" ]; then echo "  OK: $dev"; else echo "  MISS: $dev"; fi
done

echo ""
echo "--- Virtio devices ---"
for d in /sys/bus/virtio/devices/*/; do
    [ -d "$d" ] && echo "  $(basename $d)"
done

echo ""
echo "--- Block devices ---"
ls /dev/vd* 2>/dev/null || echo "  (none)"

echo ""
echo "--- Network interfaces ---"
for iface in /sys/class/net/*/; do
    [ -d "$iface" ] && echo "  $(basename $iface)"
done

echo ""
echo "=========================================="
echo "SiliconV boot successful!"
echo "=========================================="

# testmode = auto poweroff for CI/testing (print success FIRST)
if echo "${CMDLINE}" | grep -q "testmode"; then
    /bin/busybox sleep 1
    /sbin/poweroff -f
fi

echo ""
exec /bin/sh
INITEOF
chmod 755 "${STAGING}/init"

# ── Create ext4 image ─────────────────────────────

echo ""
echo "Creating ext4 rootfs image (512 MB)..."
rm -f "${OUTPUT}"

if mkfs.ext4 -d "${STAGING}" "${OUTPUT}" 512M 2>/dev/null; then
    pass "rootfs image created: ${OUTPUT} ($(du -h "${OUTPUT}" | cut -f1))"
else
    echo "  Note: mkfs.ext4 -d may need root, trying fallback..."
    fail "ext4 creation failed"
    exit 1
fi

# Cleanup staging
rm -rf "${STAGING}"

echo ""
echo "=== Done ==="
echo ""
echo "Boot with:"
echo "  qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -smp 4 -m 4G \\"
echo "    -kernel kernel/out/Image \\"
echo "    -drive file=${OUTPUT},format=raw,if=virtio \\"
echo "    -device virtio-net-device,netdev=net0 \\"
echo "    -netdev user,id=net0 \\"
echo "    -nographic -no-reboot \\"
echo "    -append 'console=ttyAMA0 root=/dev/vda rw'"
