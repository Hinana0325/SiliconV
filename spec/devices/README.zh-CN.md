# spec/devices — 设备规范

定义 SiliconV 所需的虚拟设备。

## 文档

| 文档 | 描述 |
|------|------|
| [virtio-matrix.md](virtio-matrix.md) | 必需的 virtio 设备及其配置 |

## 设备要求

每个 SiliconV 实现**必须**支持：

| 设备 | Virtio ID | 传输层 | 必需 |
|------|-----------|--------|------|
| 块设备 | 2 | MMIO | ✅ |
| 网卡 | 1 | MMIO | ✅ |
| GPU | 16 | MMIO | ✅ |
| 输入 | 18 | MMIO | ✅ |
| 控制台 | 3 | MMIO | ✅ |

以及非 virtio 设备：
- PL011 UART（串口控制台）
- GICv3（中断控制器）
- PSCI（CPU 生命周期）
