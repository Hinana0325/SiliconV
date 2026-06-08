# spec/memory — 内存布局

定义客户物理地址映射（MMIO 布局）。

## 文档

| 文档 | 描述 |
|------|------|
| [mmio-map.md](mmio-map.md) | 完整的 MMIO 地址分配 |

## 地址映射概要

```
0x00000000 - 0x3FFFFFFF   SRAM / Boot ROM（未使用）
0x08000000 - 0x080FFFFF   GICv3（分发器 + 再分发器）
0x10000000 - 0x10000FFF   PL011 UART
0x10001000 - 0x10005FFF   Virtio-MMIO 设备（每个 4KB）
0x400000000+              客户 RAM（默认 4GB）
```

## 规则

- 所有 MMIO 区域 4KB 对齐
- Virtio 设备从 `0x10001000` 开始，间隔 4KB
- 客户 RAM 基地址为 `0x400000000`（16GB 标记），为下方设备空间留出余地
