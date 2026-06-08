/*
 * SiliconV — GICv3 Emulation
 *
 * Full GICv3 (Generic Interrupt Controller v3) implementation.
 * Handles Distributor (GICD), Redistributor (GICR), and ITS (minimal).
 *
 * MMIO Base: 0x08000000 (from spec/memory/mmio-map.md)
 *
 * Reference: ARM GIC Architecture Specification v3.0/v4.0
 */

#ifndef SILICONV_GIC_H
#define SILICONV_GIC_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ─────────────────────────────────── */
#define GIC_MAX_CPU         8
#define GIC_MAX_IRQ         1024
#define GIC_SPI_BASE        32
#define GIC_SPI_COUNT       (GIC_MAX_IRQ - GIC_SPI_BASE)
#define GIC_SGI_COUNT       16
#define GIC_PPI_COUNT       16
#define GIC_NUM_LIST_REGS   64

/* ── GICD Register Offsets ─────────────────────── */
#define GICD_CTLR           0x0000
#define GICD_TYPER           0x0004
#define GICD_IIDR           0x0008
#define GICD_STATUSR        0x0010
#define GICD_SETSPI_NSR     0x0040
#define GICD_CLRSPI_NSR     0x0048
#define GICD_SETSPI_SR      0x0050
#define GICD_CLRSPI_SR      0x0058
#define GICD_SEIR           0x0068
#define GICD_IGROUPR(n)     (0x0080 + (n) * 4)
#define GICD_ISENABLER(n)   (0x0100 + (n) * 4)
#define GICD_ICENABLER(n)   (0x0180 + (n) * 4)
#define GICD_ISPENDR(n)     (0x0200 + (n) * 4)
#define GICD_ICPENDR(n)     (0x0280 + (n) * 4)
#define GICD_ISACTIVER(n)   (0x0300 + (n) * 4)
#define GICD_ICACTIVER(n)   (0x0380 + (n) * 4)
#define GICD_IPRIORITYR(n)  (0x0400 + (n) * 4)
#define GICD_ITARGETSR(n)   (0x0800 + (n) * 4)
#define GICD_ICFGR(n)       (0x0C00 + (n) * 4)
#define GICD_IGRPMODR(n)    (0x0D00 + (n) * 4)
#define GICD_NSACR(n)       (0x0E00 + (n) * 4)
#define GICD_SGIR            0x0F00
#define GICD_CPENDSGIR(n)   (0x0F10 + (n) * 4)
#define GICD_SPENDSGIR(n)   (0x0F20 + (n) * 4)
#define GICD_IROUTER(n)     (0x6000 + (n) * 8)

/* GICD_CTLR bits */
#define GICD_CTLR_ENABLE_G0     (1 << 0)
#define GICD_CTLR_ENABLE_G1_NS  (1 << 1)
#define GICD_CTLR_ENABLE_G1_S   (1 << 2)
#define GICD_CTLR_ARE_NS        (1 << 4)
#define GICD_CTLR_ARE_S         (1 << 5)
#define GICD_CTLR_DS            (1 << 6)
#define GICD_CTLR_E1NWF         (1 << 7)

/* ── GICR Register Offsets ─────────────────────── */
#define GICR_CTLR           0x0000
#define GICR_IIDR           0x0004
#define GICR_TYPER          0x0008
#define GICR_STATUSR        0x0010
#define GICR_WAKER          0x0014
#define GICR_MPAMIDR        0x0018
#define GICR_PARTID         0x001C
#define GICR_SETLPIR        0x0040
#define GICR_CLRLPIR        0x0048
#define GICR_PROPBASER      0x0070
#define GICR_PENDBASER      0x0078
#define GICR_INVLPIR        0x00A0
#define GICR_INVALLR        0x00B0
#define GICR_SYNCR          0x00C0

/* GICR SGI/PPI frame (0x10000 offset from RD_base) */
#define GICR_SGI_BASE       0x10000
#define GICR_IGROUPR0       (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0     (GICR_SGI_BASE + 0x0100)
#define GICR_ICENABLER0     (GICR_SGI_BASE + 0x0180)
#define GICR_ISPENDR0       (GICR_SGI_BASE + 0x0200)
#define GICR_ICPENDR0       (GICR_SGI_BASE + 0x0280)
#define GICR_ISACTIVER0     (GICR_SGI_BASE + 0x0300)
#define GICR_ICACTIVER0     (GICR_SGI_BASE + 0x0380)
#define GICR_IPRIORITYR(n)  (GICR_SGI_BASE + 0x0400 + (n) * 4)
#define GICR_ICFGR0         (GICR_SGI_BASE + 0x0C00)
#define GICR_ICFGR1         (GICR_SGI_BASE + 0x0C04)
#define GICR_IGRPMODR0      (GICR_SGI_BASE + 0x0D00)
#define GICR_NSACR          (GICR_SGI_BASE + 0x0E00)

/* GICR_WAKER bits */
#define GICR_WAKER_CA        (1 << 2)
#define GICR_WAKER_PS         (1 << 1)

/* ── GICR_TYPER fields ─────────────────────────── */
#define GICR_TYPER_LAST      (1ULL << 4)
#define GICR_TYPER_DPGS      (1ULL << 5)
#define GICR_TYPER_MPAM      (1ULL << 6)
#define GICR_TYPER_VLPIS     (1ULL << 7)
#define GICR_TYPER_DIRECTLPI (1ULL << 9)

/* ── Interrupt States ──────────────────────────── */
typedef enum {
    GIC_IRQ_INACTIVE = 0,
    GIC_IRQ_PENDING,
    GIC_IRQ_ACTIVE,
    GIC_IRQ_ACTIVE_PENDING,
} gic_irq_state_t;

/* ── IRQ Descriptor ────────────────────────────── */
typedef struct {
    gic_irq_state_t state;
    uint8_t         priority;       /* 0 (highest) to 255 (lowest) */
    uint8_t         targets;        /* CPU bitmask */
    uint8_t         group;          /* Group 0, 1 Secure, 1 Non-secure */
    bool            enabled;
    bool            level;          /* true = level-triggered */
    bool            config;         /* 0 = level, 1 = edge */
    uint64_t        route;          /* IROUTER affinity routing */
} gic_irq_t;

/* ── Per-CPU Redistributor State ───────────────── */
typedef struct {
    uint32_t ctlr;
    uint32_t waker;
    uint32_t gicr_igroupr0;
    uint32_t gicr_isenabler0;
    uint32_t gicr_icenabler0;
    uint32_t gicr_ispendr0;
    uint32_t gicr_icpendr0;
    uint32_t gicr_isactiver0;
    uint32_t gicr_icactiver0;
    uint8_t  gicr_ipriorityr[32];  /* SGI + PPI */
    uint32_t gicr_icfgr[2];
    uint32_t gicr_igrpmodr0;
    uint32_t gicr_nsacr;

    /* List registers for virtual interrupt delivery */
    struct {
        uint32_t apr;
        uint64_t lr[GIC_NUM_LIST_REGS];
    } list_regs;
} gic_redist_t;

/* ── GIC State ─────────────────────────────────── */
typedef struct {
    /* Distributor */
    uint32_t gicd_ctlr;
    uint32_t gicd_typer;
    uint32_t gicd_iidr;
    uint32_t gicd_statusr;

    /* IRQ descriptors (shared: SGI + PPI + SPI) */
    gic_irq_t irq[GIC_MAX_IRQ];

    /* Per-IRQ routing (SPI only, IROUTER) */
    uint64_t irouter[GIC_MAX_IRQ];

    /* Per-CPU redistributor */
    gic_redist_t redist[GIC_MAX_CPU];

    int num_cpus;

    /* IRQ line to raise (output to hypervisor) */
    void (*irq_callback)(int cpu, int irq, void *opaque);
    void *irq_opaque;
} gic_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize GIC */
void gic_init(gic_state_t *gic, int num_cpus);

/* Set IRQ callback (called when GIC wants to signal a CPU) */
void gic_set_callback(gic_state_t *gic,
                      void (*cb)(int cpu, int irq, void *opaque),
                      void *opaque);

/* Raise an SPI interrupt (device → GIC) */
void gic_raise_spi(gic_state_t *gic, int irq);

/* Lower an SPI interrupt (device → GIC) */
void gic_lower_spi(gic_state_t *gic, int irq);

/* Raise an SGI (software generated interrupt) */
void gic_raise_sgi(gic_state_t *gic, int irq, int target_cpu);

/* Acknowledge interrupt (CPU reads IAR) — returns IRQ number */
int gic_acknowledge_irq(gic_state_t *gic, int cpu);

/* End of interrupt (CPU writes EOIR) */
void gic_eoi(gic_state_t *gic, int cpu, int irq);

/* Get pending interrupt for a CPU (highest priority) */
int gic_get_pending_irq(gic_state_t *gic, int cpu);

/* MMIO handlers */
uint64_t gicd_mmio_read(gic_state_t *gic, uint64_t offset, int size);
void gicd_mmio_write(gic_state_t *gic, uint64_t offset, uint64_t value, int size);

uint64_t gicr_mmio_read(gic_state_t *gic, int cpu, uint64_t offset, int size);
void gicr_mmio_write(gic_state_t *gic, int cpu, uint64_t offset,
                     uint64_t value, int size);

#endif /* SILICONV_GIC_H */
