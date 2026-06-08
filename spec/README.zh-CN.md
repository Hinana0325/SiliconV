# spec — 硬件规范（已冻结）

SiliconV 的硬件规范。定义了虚拟平台的硬件契约、内存布局、中断分配和启动流程。

## 规范文档

| 文档 | 描述 |
|------|------|
| [SVABI v0](svabi/svabi-v0.md) | ABI 契约 |
| [硬件栈](hardware-stack.md) | 冻结的硬件选型 |
| [MMIO 布局](memory/mmio-map.md) | 设备地址映射 |
| [IRQ 布局](irq/irq-map.md) | 中断分配 |
| [启动流程](boot/boot-flow.md) | 启动序列 |
| [DTB 模式](boot/dtb-schema.md) | 设备树格式 |
| [Virtio 矩阵](devices/virtio-matrix.md) | 必需设备 |

## 规范原则

1. **冻结即不变** — v0 规范已冻结，变更需要新版本
2. **ABI 先行** — 所有变更从规范更新开始
3. **ARM64 标准** — 尽可能遵循 ARM 架构规范
4. **Virtio 1.2** — 设备接口遵循 Virtio 1.2 规范

## 冻结状态

v0 规范于 2026-05-29 冻结。只有在有充分理由时才允许更改，且必须记录在案。
