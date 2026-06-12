/*
 * SiliconV — MMIO Address Map
 *
 * Centralized MMIO base addresses and IRQ numbers for all
 * SiliconV platform devices. These values are frozen per
 * spec/memory/mmio-map.md and spec/irq/irq-map.md.
 *
 * All code MUST include this header instead of hardcoding addresses.
 * Device model implementations (PL011, GIC, etc.) should also use
 * these constants in their MMIO dispatch tables.
 */

#ifndef SILICONV_MMIO_ADDRS_H
#define SILICONV_MMIO_ADDRS_H

#include <stdint.h>

/* ═══════════════════════════════════════════════
 *  MMIO Base Addresses (from spec/memory/mmio-map.md)
 *  ═══════════════════════════════════════════════ */

/* ── Flash / BootROM ───────────────────────────── */
#define SV_ADDR_FLASH_BASE      0x00000000ULL
#define SV_ADDR_FLASH_SIZE      0x08000000ULL   /* 128 MB */
#define SV_ADDR_FLASH_END       (SV_ADDR_FLASH_BASE + SV_ADDR_FLASH_SIZE - 1)

/* ── GICv3 ──────────────────────────────────────── */
#define SV_ADDR_GICD_BASE       0x08000000ULL   /* GIC Distributor */
#define SV_ADDR_GICD_SIZE       0x00010000ULL   /* 64 KB */
#define SV_ADDR_GICD_END        (SV_ADDR_GICD_BASE + SV_ADDR_GICD_SIZE - 1)

#define SV_ADDR_GICR_BASE       0x08010000ULL   /* GIC Redistributor (per-CPU) */
#define SV_ADDR_GICR_SIZE       0x00010000ULL   /* 64 KB per CPU */
#define SV_ADDR_GICR_STRIDE     0x00020000ULL   /* 128 KB per redistributor region */

#define SV_ADDR_GIC_ITS_BASE    0x08020000ULL   /* GIC ITS (future) */
#define SV_ADDR_GIC_ITS_SIZE    0x00010000ULL   /* 64 KB */

/* ── UART / Serial ──────────────────────────────── */
#define SV_ADDR_UART0_BASE      0x10000000ULL   /* PL011 serial console */
#define SV_ADDR_UART0_SIZE      0x00010000ULL   /* 64 KB */
#define SV_ADDR_UART0_END       (SV_ADDR_UART0_BASE + SV_ADDR_UART0_SIZE - 1)

#define SV_ADDR_RTC_BASE        0x10010000ULL
#define SV_ADDR_RTC_SIZE        0x00010000ULL

#define SV_ADDR_WDT_BASE        0x10020000ULL
#define SV_ADDR_WDT_SIZE        0x00010000ULL

/* ── Virtio Devices (MMIO transport, 64K each) ──── */
#define SV_ADDR_VIRTIO_BLK      0x20000000ULL   /* Block device */
#define SV_ADDR_VIRTIO_NET      0x20010000ULL   /* Network device */
#define SV_ADDR_VIRTIO_INPUT    0x20020000ULL   /* Touchscreen/keyboard */
#define SV_ADDR_VIRTIO_GPU      0x20030000ULL   /* Framebuffer / GPU */
#define SV_ADDR_VIRTIO_CONSOLE  0x20040000ULL   /* Guest console channel */
#define SV_ADDR_VIRTIO_FS       0x20050000ULL   /* Shared filesystem (9p) */
#define SV_ADDR_VIRTIO_RNG      0x20060000ULL   /* Entropy source */
#define SV_ADDR_VIRTIO_RESERVED 0x20070000ULL   /* Future Virtio devices */

#define SV_ADDR_VIRTIO_SIZE     0x00010000ULL   /* 64 KB per device */
#define SV_ADDR_VIRTIO_END      0x200FFFFFULL   /* End of Virtio MMIO region */

/* ── Platform Bus ───────────────────────────────── */
#define SV_ADDR_PLATFORM_BUS    0x40000000ULL
#define SV_ADDR_PLATFORM_SIZE   0x10000000ULL   /* 256 MB */

/* ── Guest RAM ──────────────────────────────────── */
#define SV_ADDR_RAM_BASE        0x400000000ULL  /* 16 GB */
#define SV_ADDR_RAM_DEFAULT     0x100000000ULL  /* 4 GB default */

/* ═══════════════════════════════════════════════
 *  IRQ Numbers (from spec/irq/irq-map.md)
 *  ═══════════════════════════════════════════════ */

/* ── Platform Device IRQs ──────────────────────── */
#define SV_IRQ_UART0            32
#define SV_IRQ_RTC              33
#define SV_IRQ_WDT              34

/* ── Virtio Device IRQs ────────────────────────── */
#define SV_IRQ_VIRTIO_BLK       40
#define SV_IRQ_VIRTIO_NET       41
#define SV_IRQ_VIRTIO_INPUT     42
#define SV_IRQ_VIRTIO_GPU       43
#define SV_IRQ_VIRTIO_CONSOLE   44
#define SV_IRQ_VIRTIO_FS        45
#define SV_IRQ_VIRTIO_RNG       46

/* ── Reserved IRQ Range ────────────────────────── */
#define SV_IRQ_VIRTIO_RESERVED  47
#define SV_IRQ_VIRTIO_RESERVED_END 55

/* ── PCI Legacy IRQs ────────────────────────────── */
#define SV_IRQ_PCI_INTA         56
#define SV_IRQ_PCI_INTB         57
#define SV_IRQ_PCI_INTC         58
#define SV_IRQ_PCI_INTD         59

#endif /* SILICONV_MMIO_ADDRS_H */
