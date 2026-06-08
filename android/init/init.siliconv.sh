#!/system/bin/sh
# SiliconV Hardware Init Script
# Runs once at boot to configure virtual hardware

TAG="siliconv-init"

log -t $TAG "Starting SiliconV hardware initialization"

# ── Virtio Devices ────────────────────────────────

# Verify virtio-blk (root filesystem)
if [ -b /dev/vda ]; then
    log -t $TAG "virtio-blk: /dev/vda present"
else
    log -t $TAG "WARNING: /dev/vda not found"
fi

# Verify virtio-net
if [ -d /sys/class/net/eth0 ]; then
    log -t $TAG "virtio-net: eth0 present"
    # Set link up
    ip link set eth0 up
    # Request DHCP
    dhcpcd eth0 &
else
    log -t $TAG "WARNING: virtio-net not found"
fi

# ── Binder ────────────────────────────────────────

if [ -e /dev/binder ]; then
    log -t $TAG "binder: /dev/binder present"
else
    log -t $TAG "WARNING: /dev/binder not found"
fi

# ── Graphics ──────────────────────────────────────

# Check for DRM/KMS
if [ -d /sys/class/drm ]; then
    log -t $TAG "DRM subsystem present"
fi

# ── Display ───────────────────────────────────────

# Set framebuffer parameters
if [ -e /sys/class/graphics/fb0/virtual_size ]; then
    log -t $TAG "Framebuffer: $(cat /sys/class/graphics/fb0/virtual_size)"
fi

log -t $TAG "SiliconV hardware initialization complete"
