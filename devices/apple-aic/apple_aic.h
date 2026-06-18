/*
 * SiliconV — Apple AIC (Interrupt Controller)
 *
 * Implements Apple's custom interrupt controller used in Apple Silicon SoCs.
 * Replaces GICv3 in the Apple platform profile.
 *
 * AIC is simpler than GICv3: flat IRQ space, per-CPU register banks,
 * no SGI/PPI/SPI distinction at the architectural level.
 *
 * Reference: Apple AIC register interface (as reverse-engineered from XNU)
 */

#ifndef SILICONV_APPLE_AIC_H
#define SILICONV_APPLE_AIC_H

#include <stdint.h>
#include <stdbool.h>

/* ── Configuration ─────────────────────────────── */
#define APPLE_AIC_MAX_CPU       8
#define APPLE_AIC_NUM_IRQ       576     /* Total IRQs supported */
#define APPLE_AIC_IRQ_BASE      32      /* First usable IRQ number */
#define APPLE_AIC_NUM_SPI       (APPLE_AIC_NUM_IRQ - APPLE_AIC_IRQ_BASE)
#define APPLE_AIC_NUM_IPI       4       /* Per-CPU IPI count */

/* ── Register Offsets ──────────────────────────── */
#define AIC_REV                 0x0000
#define AIC_CAP0                0x0004
#define AIC_CAP1                0x0008
#define AIC_RST                 0x000C
#define AIC_GLB_CFG             0x0010

#define AIC_WHOAMI              0x2000
#define AIC_IACK                0x2004
#define AIC_IPI_SET             0x2008
#define AIC_IPI_CLR             0x200C

/* SPI mask/set registers (banks of 32 IRQs each) */
#define AIC_EIR_MASK_SET(n)     (0x4000 + (n) * 4)
#define AIC_EIR_MASK_CLR(n)     (0x4080 + (n) * 4)
#define AIC_EIR_SW_SET(n)       (0x4100 + (n) * 4)
#define AIC_EIR_SW_CLR(n)       (0x4180 + (n) * 4)

/* Per-CPU registers (bank size = 0x80) */
#define AIC_CPU_BASE(n)         (0x5000 + (n) * 0x80)
#define AIC_CPU_CTRL(n)         (AIC_CPU_BASE(n) + 0x00)
#define AIC_CPU_IACK(n)         (AIC_CPU_BASE(n) + 0x04)
#define AIC_CPU_EOI(n)          (AIC_CPU_BASE(n) + 0x08)
#define AIC_CPU_IPI_MASK(n)     (AIC_CPU_BASE(n) + 0x10)
#define AIC_CPU_IPI_PEND(n)     (AIC_CPU_BASE(n) + 0x14)

/* ── GLB_CFG bits ─────────────────────────────── */
#define AIC_GLB_CFG_ENABLE      (1 << 0)

/* ── CPU_CTRL bits ────────────────────────────── */
#define AIC_CPU_CTRL_ENABLE     (1 << 0)
#define AIC_CPU_CTRL_MASK_ALL   (1 << 1)

/* ── Interrupt States ──────────────────────────── */
typedef enum {
    AIC_IRQ_INACTIVE = 0,
    AIC_IRQ_PENDING,
    AIC_IRQ_ACTIVE,
    AIC_IRQ_ACTIVE_PENDING,
} aic_irq_state_t;

/* ── IRQ Descriptor ────────────────────────────── */
typedef struct {
    aic_irq_state_t state;
    bool            enabled;
    bool            masked;     /* Masked at per-CPU level */
    bool            level;      /* true = level-triggered */
    uint8_t         target_cpu; /* Destination CPU (0 = broadcast/any) */
} aic_irq_t;

/* ── AIC State ─────────────────────────────────── */
typedef struct {
    /* Global registers */
    uint32_t rev;
    uint32_t cap0;
    uint32_t cap1;
    uint32_t glb_cfg;

    /* IRQ state array */
    aic_irq_t irq[APPLE_AIC_NUM_IRQ];

    /* Per-CPU state */
    struct {
        bool    enabled;
        bool    mask_all;
        uint8_t ipi_pending;    /* Bitmask of pending IPIs */
        uint8_t ipi_mask;       /* Bitmask of enabled IPIs */
    } cpu[APPLE_AIC_MAX_CPU];

    /* Routing: which IRQ is active per CPU */
    int pending_irq[APPLE_AIC_MAX_CPU];
    int active_irq[APPLE_AIC_MAX_CPU];

    int num_cpus;

    /* IRQ output callback */
    void (*irq_callback)(int cpu, int irq, void *opaque);
    void *irq_opaque;
} apple_aic_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize AIC */
void apple_aic_init(apple_aic_state_t *aic, int num_cpus);

/* Set IRQ callback (called when AIC wants to signal/unsignal a CPU) */
void apple_aic_set_callback(apple_aic_state_t *aic,
                             void (*cb)(int cpu, int irq, void *opaque),
                             void *opaque);

/* Raise an interrupt (device → AIC) */
void apple_aic_raise_irq(apple_aic_state_t *aic, int irq);

/* Lower an interrupt (device → AIC) */
void apple_aic_lower_irq(apple_aic_state_t *aic, int irq);

/* Assert an IPI (CPU → CPU) */
void apple_aic_send_ipi(apple_aic_state_t *aic, int target_cpu, int ipi_id);

/* Acknowledge interrupt (CPU-side) — returns IRQ number */
int apple_aic_acknowledge(apple_aic_state_t *aic, int cpu);

/* End of interrupt */
void apple_aic_eoi(apple_aic_state_t *aic, int cpu, int irq);

/* Get highest priority pending IRQ for a CPU */
int apple_aic_get_pending(apple_aic_state_t *aic, int cpu);

/* MMIO handlers */
uint64_t apple_aic_mmio_read(apple_aic_state_t *aic, uint64_t offset, int size);
void     apple_aic_mmio_write(apple_aic_state_t *aic, uint64_t offset,
                               uint64_t value, int size);

#endif /* SILICONV_APPLE_AIC_H */
