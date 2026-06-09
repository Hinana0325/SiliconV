# devices/virtio-blk — Virtio 块设备

Virtio 块设备——为客户提供虚拟磁盘（根文件系统）。

## 概述

客户将标准 virtio-blk 设备视为其根文件系统。宿主使用原始磁盘镜像文件作为后端。所有块 I/O 通过 virtqueue 进行。

## Virtqueue 布局

| 队列 | 描述 |
|------|------|
| 0 | 请求队列（读/写/刷新） |

## 请求协议

```
客户 → 设备:
  struct virtio_blk_req {
      uint32_t type;    // VIRTIO_BLK_T_IN（读）/ _OUT（写）/ _FLUSH
      uint32_t reserved;
      uint64_t sector;  // LBA 扇区号
      // 数据载荷
      uint8_t status;   // 结果（由设备回写）
  };
```

## 支持的功能

| 功能 | 位 | 描述 |
|------|-----|------|
| `BLK_F_SIZE_MAX` | 1 | 最大段大小 |
| `BLK_F_SEG_MAX` | 2 | 最大段数 |
| `BLK_F_GEOMETRY` | 4 | 磁盘几何参数（柱面/磁头/扇区） |
| `BLK_F_RO` | 5 | 只读磁盘 |
| `BLK_F_BLK_SIZE` | 6 | 逻辑块大小 |
| `BLK_F_FLUSH` | 9 | 缓存刷新命令 |
| `BLK_F_TOPOLOGY` | 10 | 物理拓扑信息 |

## 文件

| 文件 | 描述 |
|------|------|
| `virtio_blk.c` / `virtio_blk.h` | 块设备实现 |

## 使用方法

```bash
# 创建磁盘镜像
dd if=/dev/zero of=rootfs.img bs=1M count=4096

# 格式化
mkfs.ext4 rootfs.img

# 使用磁盘运行
./build/sv-cli -k Image -r rootfs.img
```

## 参考

- Virtio 1.2 规范，第 5.2 节（块设备）
