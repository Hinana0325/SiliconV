# Apple Profile — IRQ Mapping

This document defines the IRQ numbers for the Apple virtual platform profile
(SVABI-Apple v0). Apple AIC uses a flat IRQ numbering scheme.

## AIC Interrupt Model

Apple AIC (Apple Interrupt Controller) uses:
- **SPI** (Shared Peripheral Interrupt): Shared level-triggered interrupts
- **IPI** (Inter-Processor Interrupt): Per-CPU inter-processor interrupts
- **Timer**: Per-CPU virtual timer interrupt

Unlike GICv3, AIC uses a simpler flat IRQ number space with no SGI/PPI/SPI
distinction. All device interrupts are in the same range and are masked/unmasked
via per-CPU register banks.

## IRQ Assignments

| IRQ | Device | Type | Notes |
|-----|--------|------|-------|
| 1 | Virtual Timer | PPI | Per-CPU, from ARM generic timer |
| 2 | HV Timer | PPI | Per-CPU, from hypervisor |
| 32 | UART0 | SPI | Serial console |
| 33 | UART1 | SPI | Debug serial |
| 34 | DART0 | SPI | Display IOMMU |
| 35 | DART1 | SPI | Storage IOMMU |
| 36 | SEP | SPI | Secure Enclave |
| 37 | WDT | SPI | Watchdog timer |
| 38 | NVRAM | SPI | NVRAM controller |
| 39 | GPIO | SPI | GPIO controller |
| 40 | I2C0 | SPI | I2C bus 0 |
| 41 | SPMI | SPI | SPMI controller |
| 42 | PCIe | SPI | PCIe host bridge |
| 43 | TIMER | SPI | Apple timer |
| 44-47 | IPI_0-3 | IPI | Per-CPU IPIs |
| 48 | Virtio-BLK0 | SPI | Block device #0 |
| 49 | Virtio-NET | SPI | Network device |
| 50 | Virtio-CONSOLE | SPI | Console channel |
| 51 | Virtio-BLK1 | SPI | Block device #1 |
| 52 | NVMe | SPI | NVMe storage controller |
| 53-63 | Reserved | SPI | Future devices |

## IRQ Constants

These IRQ numbers are defined as C macros in:
`core/memory/mmio_addrs.h` (Apple section)

## AIC Register Interface

```
AIC Base: 0x03000000

Offset  Register            Description
------  --------            -----------
0x000   AIC_REV             Revision ID
0x004   AIC_CAP0            Capabilities 0
0x008   AIC_CAP1            Capabilities 1
0x00C   AIC_RST             Reset (write-only)
0x010   AIC_GLB_CFG         Global configuration

0x2000  AIC_WHOAMI          CPU ID read
0x2004  AIC_IACK            Interrupt Acknowledge
0x2008  AIC_IPI_SET         IPI set (write-only)
0x200C  AIC_IPI_CLR         IPI clear (write-only)

0x4000  AIC_EIR_MASK_SET0   SPI mask set (IRQ 32-63)
0x4080  AIC_EIR_MASK_CLR0   SPI mask clear (IRQ 32-63)
0x4100  AIC_EIR_SW_SET0     Software IRQ set
0x4180  AIC_EIR_SW_CLR0     Software IRQ clear

Per-CPU registers (0x5000 + cpu_id * 0x80):
0x5000  AIC_CPU_CTRL        CPU control
0x5004  AIC_CPU_IACK        CPU interrupt acknowledge
0x5008  AIC_CPU_EOI         CPU end-of-interrupt
0x5010  AIC_CPU_IPI_MASK    CPU IPI mask
0x5014  AIC_CPU_IPI_PEND    CPU IPI pending
```
