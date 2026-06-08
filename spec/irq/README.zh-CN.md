# spec/irq — IRQ 布局

定义所有虚拟设备的中断分配。

## 文档

| 文档 | 描述 |
|------|------|
| [irq-map.md](irq-map.md) | 完整的 IRQ 分配表 |

## IRQ 概要

| IRQ | 类型 | 设备 |
|-----|------|------|
| 0–15 | SGI | 软件生成（IPI） |
| 16–31 | PPI | 每 CPU 私有中断 |
| 32 | SPI | PL011 UART |
| 33 | SPI | Virtio-Blk |
| 34 | SPI | Virtio-Net |
| 35 | SPI | Virtio-GPU |
| 36 | SPI | Virtio-Input |
| 37 | SPI | Virtio-Console |

## 规则

- SPI IRQ 从 32 开始（GIC_SPI_BASE）
- IRQ 按设备顺序分配
- 每个 virtio 设备为所有 virtqueue 分配一个 IRQ
