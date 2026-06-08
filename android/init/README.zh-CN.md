# SiliconV — Android Init 集成

> Android init 是 Linux 内核与 Android 框架之间的桥梁。
> 让 init 运行起来是第一个真正的 Android 里程碑。

## 启动阶段

### 第一阶段 Init（`init_first_stage`）

在分区挂载之前运行。

```
内核启动
  → /init（第一阶段）
    → 挂载 rootfs（或切换根）
    → 加载内核模块（binder、ashmem、virtio 驱动）
    → 设置 devtmpfs
    → 挂载 /proc、/sys、/dev
    → 加载 SELinux 策略
    → 执行第二阶段 init
```

**SiliconV 要求：**
- virtio-blk 用于 rootfs（`/dev/vda`）
- 内核模块内置（非可加载）：binder、ashmem、virtio-*
- SELinux 策略在 vendor 分区或烘焙进 ramdisk

### 第二阶段 Init（`init`）

真正的 init。

```
init
  → 解析 init.rc
  → 挂载分区（/system、/vendor、/data）
  → 启动服务（servicemanager、vold、logd 等）
  → 启动 zygote
```

**SiliconV 要求：**
- 正确的 fstab 分区布局
- servicemanager（binder）启动
- logd 启动（logcat 可用）
- vold 启动（存储管理）

### Zygote

Android 应用进程工厂。

```
zygote
  → 预加载 Java 类
  → 预加载资源
  → 启动 system_server
```

**SiliconV 要求：**
- Binder 正常工作
- 共享内存（ashmem）正常工作
- 足够的 RAM（最少 4G）

### System Server

Android 系统核心。

```
system_server
  → WindowManagerService
  → ActivityManagerService
  → PackageManagerService
  → SurfaceFlinger（通过 HWC HAL）
  → ...
```

## SiliconV Init 配置

### fstab（文件系统表）

```
# fstab.siliconv
# <src>                                   <挂载点>      <类型>  <挂载标志>                               <fs_mgr_flags>
/dev/vda                                   /            ext4    ro,barrier=1                             wait,first_stage_mount
/dev/vb                                    /vendor      ext4    ro,barrier=1                             wait,logical,first_stage_mount
/dev/vc                                    /system      ext4    ro,barrier=1                             wait,logical,first_stage_mount
/dev/vd                                    /data        ext4    noatime,nosuid,nodev,barrier=1           wait,check,fileencryption=software
tmpfs                                      /tmp         tmpfs   nodev,nosuid,relatime,mode=1777          bind
```

### init.rc（关键服务）

```
# init.siliconv.rc

on early-init
    # 设置 SiliconV 硬件属性
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
    # 创建数据目录
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

## 硬件属性

```properties
# SiliconV 系统属性

# 设备标识
ro.product.model=SiliconV
ro.product.brand=SiliconV
ro.product.name=siliconv
ro.product.device=siliconv

# 硬件
ro.hardware=siliconv
ro.hardware.chipname=siliconv
ro.boot.hardware=siliconv

# Android
ro.build.type=userdebug
ro.debuggable=1
persist.sys.usb.config=adb

# 显示
ro.sf.lcd_density=420
vendor.display.size=1080x2400

# Binder
ro.binder.size.default=1

# SELinux
ro.boot.selinux=permissive
```

## 里程碑：Init 启动

**成功标准：**
- [ ] 内核启动到 init
- [ ] 第一阶段 init 完成（SELinux 已加载）
- [ ] 第二阶段 init 启动
- [ ] servicemanager 启动
- [ ] logd 启动
- [ ] `logcat` 显示输出
- [ ] zygote 启动
- [ ] system_server 启动

**调试：** 通过 virtio-console 或 UART 使用 `logcat -b all`。
