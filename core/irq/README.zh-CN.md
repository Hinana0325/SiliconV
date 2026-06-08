# core/irq — GICv3 模拟

完整的 GICv3（通用中断控制器 v3）实现。

## 概述

GIC 负责将设备中断路由到 vCPU。SiliconV 模拟了完整的 GICv3：

- **GICD**（分发器）— 全局中断路由
- **GICR**（再分发器）— 每 CPU 中断状态
- **ITS**（中断转换服务）— 最小化实现，支持 MSI

## 中断映射

| 范围 | 类型 | 描述 |
|------|------|------|
| 0–15 | SGI | 软件生成的处理器间中断 |
| 16–31 | PPI | 私有外设中断（每 CPU） |
| 32–1019 | SPI | 共享外设中断（设备） |

设备 IRQ 分配详见 [spec/irq/irq-map.md](../spec/irq/irq-map.md)。

## MMIO 区域

| 基地址 | 大小 | 组件 |
|--------|------|------|
| `0x08000000` | 64KB | GICD（分发器） |
| `0x08040000` | 128KB | GICR（再分发器，每 CPU） |
| `0x08080000` | 64KB | ITS（可选） |

## 文件

| 文件 | 描述 |
|------|------|
| `gic.c` / `gic.h` | GICv3 模拟实现 |

## 参考

- ARM GIC 架构规范 v3.0 / v4.0
- Linux 内核：`drivers/irqchip/irq-gic-v3.c`
