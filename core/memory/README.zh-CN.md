# core/memory — DTB 生成器

运行时扁平设备树（FDT/DTB）生成器。生成描述 SiliconV 虚拟硬件的设备树 blob，供客户内核使用。

## 为什么是运行时生成？

SiliconV 不附带静态 `.dtb` 文件，而是在启动时根据以下配置动态生成设备树：

- 配置的 vCPU 数量
- 客户 RAM 大小
- 挂载的 virtio 设备
- 规范定义的 MMIO 和 IRQ 布局

这样 DTB 始终与实际 VM 配置保持同步。

## 设计

- **无 libfdt 依赖** — 直接写入二进制 FDT 格式
- 遵循 [spec/boot/dtb-schema.md](../spec/boot/dtb-schema.md) 定义的 DTB 模式
- MMIO 地址匹配 [spec/memory/mmio-map.md](../spec/memory/mmio-map.md)
- IRQ 编号匹配 [spec/irq/irq-map.md](../spec/irq/irq-map.md)

## 文件

| 文件 | 描述 |
|------|------|
| `dtb.c` / `dtb.h` | DTB 二进制生成器 |

## 输出结构

```
/ {
    #address-cells = <2>;
    #size-cells = <2>;
    compatible = "siliconv,virtual-phone";
    model = "SiliconV v0";

    cpus { ... };           // num_cpus × Cortex-A76
    memory@400000000 { ... }; // ram_base + ram_size
    interrupt-controller@8000000 { ... }; // GICv3
    serial@10000000 { ... };  // PL011 UART
    virtio_mmio@10001000 { ... }; // virtio-blk
    ...
};
```
