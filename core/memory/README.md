# core/memory — DTB Generator

Runtime Flattened Device Tree (FDT/DTB) generator. Produces the device tree blob that describes the SiliconV virtual hardware to the guest kernel.

## Why Runtime Generation?

Instead of shipping a static `.dtb` file, SiliconV generates the device tree at launch time based on:

- Number of vCPUs configured
- Amount of guest RAM
- Attached virtio devices
- Spec-defined MMIO and IRQ layouts

This keeps the DTB always in sync with the actual VM configuration.

## Design

- **No libfdt dependency** — writes the binary FDT format directly
- Follows the DTB schema defined in [spec/boot/dtb-schema.md](../spec/boot/dtb-schema.md)
- MMIO addresses match [spec/memory/mmio-map.md](../spec/memory/mmio-map.md)
- IRQ numbers match [spec/irq/irq-map.md](../spec/irq/irq-map.md)

## Files

| File | Description |
|------|-------------|
| `dtb.c` / `dtb.h` | DTB binary generator |

## Output Structure

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
