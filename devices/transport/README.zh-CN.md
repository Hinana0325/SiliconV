# devices/transport — Virtio MMIO 传输层

实现 virtio-mmio 传输层——连接 virtio 设备与客户内核的总线。

## 概述

每个 virtio 设备以 MMIO 区域的形式暴露。客户内核通过从固定 MMIO 地址读取设备 ID 来发现设备。传输层处理：

- 设备初始化和功能协商
- Virtqueue 设置和通知
- 完成时的中断注入

## MMIO 寄存器布局

每个设备占用 4KB MMIO 区域，寄存器映射如下：

| 偏移 | 名称 | 访问 | 描述 |
|------|------|------|------|
| `0x000` | MagicValue | R | `0x74726976`（"virt"） |
| `0x004` | Version | R | `2`（现代 virtio） |
| `0x008` | DeviceID | R | 设备类型（1=网卡, 2=块设备, 16=GPU, ...） |
| `0x00C` | VendorID | R | `0x554D4551`（"QEMU"） |
| `0x010` | DeviceFeatures | R | 提供的功能位 |
| `0x020` | DriverFeatures | W | 接受的功能位 |
| `0x030` | QueueSel | W | 选择 virtqueue |
| `0x034` | QueueNumMax | R | 最大队列大小 |
| `0x038` | QueueNum | W | 队列大小 |
| `0x044` | QueueReady | RW | 队列就绪标志 |
| `0x050` | QueueNotify | W | 通知设备 |
| `0x060` | InterruptStatus | R | 待处理中断 |
| `0x064` | InterruptAck | W | 确认中断 |

## 设备地址分配

设备从 `0x10001000` 开始，间隔 4KB：

```
0x10001000  virtio-blk  (DeviceID=2)
0x10002000  virtio-net  (DeviceID=1)
0x10003000  virtio-gpu  (DeviceID=16)
0x10004000  virtio-input (DeviceID=18)
0x10005000  virtio-console (DeviceID=3)
```

## 文件

| 文件 | 描述 |
|------|------|
| `virtio_mmio.c` / `virtio_mmio.h` | 传输层实现 |

## 参考

- Virtio 1.2 规范，第 4.2.4 节（Legacy/MMIO）
- Linux 内核：`drivers/virtio/virtio_mmio.c`
