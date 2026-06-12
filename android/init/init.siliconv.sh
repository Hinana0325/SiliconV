#!/system/bin/sh
# SiliconV Hardware Verification Script
# Runs at boot to validate virtual hardware and Android-required devices.
# Triggered by siliconv-hw service in init.siliconv.rc.

TAG="siliconv-hw"

log -t $TAG "Starting SiliconV hardware verification"

# ── Virtio Block Devices ─────────────────────────

# System partition (virtio-blk, QEMU -drive file=system.img)
if [ -b /dev/vda ]; then
    log -t $TAG "virtio-blk: /dev/vda present (system)"
else
    log -t $TAG "ERROR: /dev/vda not found (system partition missing)"
fi

# Vendor partition (virtio-blk, QEMU -drive file=vendor.img)
if [ -b /dev/vdb ]; then
    log -t $TAG "virtio-blk: /dev/vdb present (vendor)"
else
    log -t $TAG "ERROR: /dev/vdb not found (vendor partition missing)"
fi

# ── Binder Devices ───────────────────────────────

for bdev in binder hwbinder vndbinder; do
    if [ -e /dev/$bdev ]; then
        log -t $TAG "binder: /dev/$bdev present"
    else
        log -t $TAG "ERROR: /dev/$bdev not found"
    fi
done

# ── Device-Mapper (Phase 4: dynamic partitions) ──

if [ -e /dev/device-mapper ]; then
    log -t $TAG "device-mapper: /dev/device-mapper present"
else
    log -t $TAG "ERROR: /dev/device-mapper not found (required for dynamic partitions)"
fi

# ── DMA-BUF Heaps ────────────────────────────────

if [ -d /dev/dma_heap ]; then
    log -t $TAG "dma_heap: /dev/dma_heap present"
    if [ -e /dev/dma_heap/system ]; then
        log -t $TAG "dma_heap: /dev/dma_heap/system present"
    else
        log -t $TAG "WARNING: /dev/dma_heap/system not found"
    fi
else
    log -t $TAG "WARNING: /dev/dma_heap not found"
fi

# ── Virtio-NET ────────────────────────────────────

if [ -d /sys/class/net/eth0 ]; then
    log -t $TAG "virtio-net: eth0 present"
    ip link set eth0 up 2>/dev/null
else
    log -t $TAG "WARNING: virtio-net not found"
fi

# ── SELinux Status ───────────────────────────────

if [ -e /sys/fs/selinux/enforce ]; then
    SEL_MODE=$(cat /sys/fs/selinux/enforce 2>/dev/null || echo "unknown")
    case "$SEL_MODE" in
        1) log -t $TAG "SELinux: enforcing" ;;
        0) log -t $TAG "SELinux: permissive" ;;
        *) log -t $TAG "SELinux: status unknown ($SEL_MODE)" ;;
    esac
else
    log -t $TAG "WARNING: SELinux filesystem not mounted"
fi

# ── Kernel Command Line ──────────────────────────

if [ -r /proc/cmdline ]; then
    CMDLINE=$(cat /proc/cmdline 2>/dev/null)
    log -t $TAG "kernel cmdline: $CMDLINE"

    # Extract and log androidboot.* properties
    for prop in $(echo "$CMDLINE" | tr ' ' '\n' | grep '^androidboot\.'); do
        log -t $TAG "  $prop"
    done
else
    log -t $TAG "WARNING: /proc/cmdline not readable"
fi

log -t $TAG "SiliconV hardware verification complete"
