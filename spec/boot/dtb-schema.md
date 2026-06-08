# SiliconV DTB Schema v0

> Frozen: 2026-05-29
> Status: Draft — Phase 0 Specification Freeze

## Overview

The SiliconV Device Tree Blob (DTB) describes the virtual hardware to the guest kernel.
It must match the MMIO and IRQ layouts exactly.

## Root Node

```dts
/dts-v1/;

/ {
    model = "SiliconV Virtual Machine";
    compatible = "siliconv,vm-v0";
    #address-cells = <2>;
    #size-cells = <2>;

    chosen { ... };
    memory { ... };
    cpus { ... };
    gic { ... };
    uart { ... };
    virtio_devices { ... };
};
```

## CPU Topology

```dts
cpus {
    #address-cells = <1>;
    #size-cells = <0>;

    // Default: 4x Cortex-A55 (little cores)
    cpu@0 {
        device_type = "cpu";
        compatible = "arm,cortex-a55";
        reg = <0x0>;
        enable-method = "psci";
    };

    cpu@1 {
        device_type = "cpu";
        compatible = "arm,cortex-a55";
        reg = <0x1>;
        enable-method = "psci";
    };

    cpu@2 {
        device_type = "cpu";
        compatible = "arm,cortex-a55";
        reg = <0x2>;
        enable-method = "psci";
    };

    cpu@3 {
        device_type = "cpu";
        compatible = "arm,cortex-a55";
        reg = <0x3>;
        enable-method = "psci";
    };
};
```

> v0 uses homogeneous cores only. big.LITTLE deferred to v1.

## Memory

```dts
memory@400000000 {
    device_type = "memory";
    reg = <0x00000004 0x00000000 0x00000001 0x00000000>;  // 4G at 16G
};
```

## PSCI (Power State Coordination Interface)

```dts
psci {
    compatible = "arm,psci-1.0";
    method = "hvc";
};
```

## GICv3

```dts
gic: interrupt-controller@8000000 {
    compatible = "arm,gic-v3";
    #interrupt-cells = <3>;
    #address-cells = <2>;
    #size-cells = <2>;
    interrupt-controller;
    reg = <0x0 0x08000000 0 0x10000>,  // GICD
          <0x0 0x08010000 0 0x10000>,  // GICR
          <0x0 0x08020000 0 0x10000>;  // GITS
    ranges;
};
```

## UART (PL011)

```dts
uart@10000000 {
    compatible = "arm,pl011", "arm,primecell";
    reg = <0x0 0x10000000 0 0x10000>;
    interrupts = <GIC_SPI 32 IRQ_TYPE_LEVEL_HIGH>;
    clock-frequency = <24000000>;
    status = "okay";
};
```

## Virtio Devices (MMIO Transport)

```dts
virtio_blk@20000000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20000000 0 0x200>;
    interrupts = <GIC_SPI 40 IRQ_TYPE_LEVEL_HIGH>;
};

virtio_net@20010000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20010000 0 0x200>;
    interrupts = <GIC_SPI 41 IRQ_TYPE_LEVEL_HIGH>;
};

virtio_input@20020000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20020000 0 0x200>;
    interrupts = <GIC_SPI 42 IRQ_TYPE_LEVEL_HIGH>;
};

virtio_gpu@20030000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20030000 0 0x200>;
    interrupts = <GIC_SPI 43 IRQ_TYPE_LEVEL_HIGH>;
};

virtio_console@20040000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20040000 0 0x200>;
    interrupts = <GIC_SPI 44 IRQ_TYPE_LEVEL_HIGH>;
};

virtio_fs@20050000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20050000 0 0x200>;
    interrupts = <GIC_SPI 45 IRQ_TYPE_LEVEL_HIGH>;
};

virtio_rng@20060000 {
    compatible = "virtio,mmio";
    reg = <0x0 0x20060000 0 0x200>;
    interrupts = <GIC_SPI 46 IRQ_TYPE_LEVEL_HIGH>;
};
```

## Chosen Node (Boot Parameters)

```dts
chosen {
    bootargs = "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw";
    stdout-path = "/uart@10000000";
};
```

## Timer

```dts
timer {
    compatible = "arm,armv8-timer";
    interrupts = <GIC_PPI 13 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>,
                 <GIC_PPI 14 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>,
                 <GIC_PPI 11 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>,
                 <GIC_PPI 10 (GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW)>;
};
```

## DTB Generation

The hypervisor runtime generates the DTB at VM creation time based on:

1. VM profile (CPU count, RAM size)
2. Enabled Virtio devices
3. MMIO map (`spec/memory/mmio-map.md`)
4. IRQ map (`spec/irq/irq-map.md`)

**No hardcoded DTB** — always generated from spec constants.

## Validation

A DTB is valid SiliconV v0 if:

- [ ] All MMIO addresses match `spec/memory/mmio-map.md`
- [ ] All IRQ numbers match `spec/irq/irq-map.md`
- [ ] `compatible` includes `"siliconv,vm-v0"`
- [ ] GIC node has exactly 3 reg entries (GICD, GICR, GITS)
- [ ] UART0 is at `0x10000000` with IRQ 32
- [ ] `stdout-path` points to UART0
