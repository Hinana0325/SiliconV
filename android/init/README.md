# SiliconV — Android Init Integration

> Android init is the bridge between Linux kernel and Android framework.
> Getting init to run is the first real Android milestone.

## Boot Stages

### First Stage Init (`init_first_stage`)

Runs before partitions are mounted.

```
Kernel boots
  → /init (first_stage)
    → Mounts rootfs (or switches root)
    → Loads kernel modules (binder, ashmem, virtio drivers)
    → Sets up devtmpfs
    → Mounts /proc, /sys, /dev
    → Loads SELinux policy
    → Execs second_stage init
```

**SiliconV requirements:**
- virtio-blk for rootfs (`/dev/vda`)
- Kernel modules built-in (not loadable) for: binder, ashmem, virtio-*
- SELinux policy in vendor partition or baked into ramdisk

### Second Stage Init (`init`)

The real init.

```
init
  → Parses init.rc
  → Mounts partitions (/system, /vendor, /data)
  → Starts services (servicemanager, vold, logd, etc.)
  → Starts zygote
```

**SiliconV requirements:**
- Correct fstab for partition layout
- servicemanager (binder) starts
- logd starts (logcat works)
- vold starts (storage management)

### Zygote

The Android app process factory.

```
zygote
  → Preloads Java classes
  → Preloads resources
  → Starts system_server
```

**SiliconV requirements:**
- Binder working
- Shared memory (ashmem) working
- Sufficient RAM (4G minimum)

### System Server

The Android system.

```
system_server
  → WindowManagerService
  → ActivityManagerService
  → PackageManagerService
  → SurfaceFlinger (via HWC HAL)
  → ...
```

## SiliconV Init Configuration

### fstab (File System Table)

```
# fstab.siliconv
# <src>                                   <mnt_point>  <type>  <mnt_flags>                              <fs_mgr_flags>
/dev/vda                                   /            ext4    ro,barrier=1                             wait,first_stage_mount
/dev/vb                                    /vendor      ext4    ro,barrier=1                             wait,logical,first_stage_mount
/dev/vc                                    /system      ext4    ro,barrier=1                             wait,logical,first_stage_mount
/dev/vd                                    /data        ext4    noatime,nosuid,nodev,barrier=1           wait,check,fileencryption=software
tmpfs                                      /tmp         tmpfs   nodev,nosuid,relatime,mode=1777          bind
```

### init.rc (Key Services)

```
# init.siliconv.rc

on early-init
    # Set SiliconV hardware property
    setprop ro.hardware siliconv
    setprop ro.hardware.chipname siliconv
    setprop ro.boot.hardware siliconv

on init
    # Binder
    mkdir /dev/binderfs 0755 root root
    mount binder binder /dev/binderfs

    # Ashmem
    chmod 0666 /dev/ashmem

    # ION / DMABUF
    chmod 0666 /dev/ion
    chmod 0666 /dev/dma_heap/system

on post-fs-data
    # Create data directories
    mkdir /data/misc 01771 system system
    mkdir /data/system 0775 system system
    mkdir /data/app 0771 system system

service servicemanager /system/bin/servicemanager
    class core
    user system
    group system readproc
    critical
    onrestart restart zygote
    onrestart restart audioserver
    writepid /dev/cpuset/foreground/tasks

service logd /system/bin/logd
    class core
    socket logd stream 0666 logd logd
    socket logdr seqpacket 0666 logd logd
    socket logdw dgram+passcred 0222 logd logd

service zygote /system/bin/app_process64 -Xzygote /system/bin --zygote --start-system-server
    class main
    priority -20
    user root
    group root readproc reserved_disk
    socket zygote stream 660 root system
    socket usap_pool_primary stream 660 root system
    onrestart exec_background - system system -- /system/bin/vdc volume abort_fuse
    onrestart write /sys/power/state on
    onrestart restart audioserver
    onrestart restart cameraserver
    onrestart restart media
    task_profiles ProcessCapacityHigh MaxPerformance
    critical window=30s
```

## Hardware Properties

```properties
# System properties for SiliconV

# Device identity
ro.product.model=SiliconV
ro.product.brand=SiliconV
ro.product.name=siliconv
ro.product.device=siliconv

# Hardware
ro.hardware=siliconv
ro.hardware.chipname=siliconv
ro.boot.hardware=siliconv

# Android
ro.build.type=userdebug
ro.debuggable=1
persist.sys.usb.config=adb

# Display
ro.sf.lcd_density=420
vendor.display.size=1080x2400

# Binder
ro.binder.size.default=1

# SELinux
ro.boot.selinux=permissive
```

## Milestone: Init Boot

**Success criteria:**
- [ ] Kernel boots to init
- [ ] First stage init completes (SELinux loaded)
- [ ] Second stage init starts
- [ ] servicemanager starts
- [ ] logd starts
- [ ] `logcat` shows output
- [ ] zygote starts
- [ ] system_server starts

**Debug:** Use `logcat -b all` via virtio-console or UART.
