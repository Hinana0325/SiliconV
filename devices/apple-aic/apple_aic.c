/*
 * SiliconV — Apple AIC Interrupt Controller (Implementation)
 *
 * Implements Apple's custom interrupt controller.
 * Flat IRQ space with per-CPU register banks, IPI support,
 * and level/edge-triggered IRQ handling.
 *
 * This replaces GICv3 when the Apple platform profile is active.
 */

#include "apple_aic.h"
#include <string.h>
#include <stdio.h>

void apple_aic_init(apple_aic_state_t *aic, int num_cpus)
{
    memset(aic, 0, sizeof(*aic));

    aic->num_cpus = (num_cpus < APPLE_AIC_MAX_CPU) ? num_cpus : APPLE_AIC_MAX_CPU;
    aic->rev = 0x00010000;          /* v1.0 */
    aic->cap0 = APPLE_AIC_NUM_IRQ;  /* Number of IRQs */
    aic->cap1 = 0;                   /* No special capabilities */

    /* Initialize per-CPU state */
    for (int i = 0; i < aic->num_cpus; i++) {
        aic->cpu[i].enabled = true;
        aic->cpu[i].mask_all = false;
        aic->cpu[i].ipi_mask = 0xFF;  /* All IPIs enabled by default */
        aic->pending_irq[i] = -1;
        aic->active_irq[i] = -1;
    }
}

void apple_aic_set_callback(apple_aic_state_t *aic,
                             void (*cb)(int cpu, int irq, void *opaque),
                             void *opaque)
{
    aic->irq_callback = cb;
    aic->irq_opaque = opaque;
}

/* ── Internal: signal IRQ to a target CPU ───────── */
static void aic_signal_cpu(apple_aic_state_t *aic, int cpu, int irq)
{
    if (aic->irq_callback) {
        aic->irq_callback(cpu, irq, aic->irq_opaque);
    }
}

/* ── Internal: find best CPU to handle an IRQ ──── */
static int aic_find_target_cpu(apple_aic_state_t *aic, int irq)
{
    if (irq < 0 || irq >= APPLE_AIC_NUM_IRQ)
        return 0;

    /* If IRQ has specific target, use it */
    if (aic->irq[irq].target_cpu < aic->num_cpus)
        return aic->irq[irq].target_cpu;

    /* Otherwise, find the first CPU that is enabled and not masked */
    for (int i = 0; i < aic->num_cpus; i++) {
        if (aic->cpu[i].enabled && !aic->cpu[i].mask_all)
            return i;
    }
    return 0;
}

/* ── Update pending state after changes ───────── */
static void aic_update(apple_aic_state_t *aic)
{
    for (int cpu = 0; cpu < aic->num_cpus; cpu++) {
        /* Find highest priority pending IRQ for this CPU */
        for (int irq = APPLE_AIC_IRQ_BASE; irq < APPLE_AIC_NUM_IRQ; irq++) {
            if (aic->irq[irq].state == AIC_IRQ_PENDING ||
                aic->irq[irq].state == AIC_IRQ_ACTIVE_PENDING) {

                if (!aic->irq[irq].enabled || aic->irq[irq].masked)
                    continue;

                int target = aic_find_target_cpu(aic, irq);

                /* Check if this IRQ targets this CPU */
                if (target == cpu || aic->irq[irq].target_cpu >= aic->num_cpus) {
                    /* IPIs are handled separately */
                    if (irq < APPLE_AIC_IRQ_BASE)
                        continue;

                    aic->pending_irq[cpu] = irq;
                    aic_signal_cpu(aic, cpu, irq);
                    return;
                }
            }
        }

        /* No pending IRQ for this CPU */
        aic->pending_irq[cpu] = -1;
    }
}

/* ── Public API ─────────────────────────────────── */

void apple_aic_raise_irq(apple_aic_state_t *aic, int irq)
{
    if (irq < 0 || irq >= APPLE_AIC_NUM_IRQ)
        return;

    if (!aic->irq[irq].enabled)
        aic->irq[irq].enabled = true;

    if (aic->irq[irq].state == AIC_IRQ_INACTIVE) {
        aic->irq[irq].state = AIC_IRQ_PENDING;
        aic_update(aic);
    } else if (aic->irq[irq].state == AIC_IRQ_ACTIVE) {
        aic->irq[irq].state = AIC_IRQ_ACTIVE_PENDING;
    }
}

void apple_aic_lower_irq(apple_aic_state_t *aic, int irq)
{
    if (irq < 0 || irq >= APPLE_AIC_NUM_IRQ)
        return;

    if (aic->irq[irq].level) {
        /* Level-triggered: only clear if not in active state */
        if (aic->irq[irq].state == AIC_IRQ_PENDING) {
            aic->irq[irq].state = AIC_IRQ_INACTIVE;
            aic_update(aic);
        } else if (aic->irq[irq].state == AIC_IRQ_ACTIVE_PENDING) {
            aic->irq[irq].state = AIC_IRQ_ACTIVE;
        }
    }
}

void apple_aic_send_ipi(apple_aic_state_t *aic, int target_cpu, int ipi_id)
{
    if (target_cpu < 0 || target_cpu >= aic->num_cpus)
        return;
    if (ipi_id < 0 || ipi_id >= APPLE_AIC_NUM_IPI)
        return;

    aic->cpu[target_cpu].ipi_pending |= (1 << ipi_id);
    aic_signal_cpu(aic, target_cpu, APPLE_AIC_IRQ_BASE + ipi_id);
}

int apple_aic_acknowledge(apple_aic_state_t *aic, int cpu)
{
    if (cpu < 0 || cpu >= aic->num_cpus)
        return -1;

    /* Check IPIs first */
    if (aic->cpu[cpu].ipi_pending & aic->cpu[cpu].ipi_mask) {
        /* Find highest priority pending IPI */
        for (int i = 0; i < APPLE_AIC_NUM_IPI; i++) {
            if (aic->cpu[cpu].ipi_pending & (1 << i)) {
                aic->cpu[cpu].ipi_pending &= ~(1 << i);
                int irq = APPLE_AIC_IRQ_BASE + i;
                aic->active_irq[cpu] = irq;
                aic->irq[irq].state = AIC_IRQ_ACTIVE;
                return irq;
            }
        }
    }

    /* Check pending IRQs */
    int irq = aic->pending_irq[cpu];
    if (irq >= 0 && irq < APPLE_AIC_NUM_IRQ &&
        aic->irq[irq].state != AIC_IRQ_INACTIVE) {

        if (aic->irq[irq].state == AIC_IRQ_PENDING) {
            aic->irq[irq].state = AIC_IRQ_ACTIVE;
        } else if (aic->irq[irq].state == AIC_IRQ_ACTIVE_PENDING) {
            aic->irq[irq].state = AIC_IRQ_ACTIVE;
        }

        aic->active_irq[cpu] = irq;
        aic->pending_irq[cpu] = -1;
        return irq;
    }

    return -1; /* No pending IRQ */
}

void apple_aic_eoi(apple_aic_state_t *aic, int cpu, int irq)
{
    if (cpu < 0 || cpu >= aic->num_cpus)
        return;
    if (irq < 0 || irq >= APPLE_AIC_NUM_IRQ)
        return;

    if (aic->active_irq[cpu] == irq) {
        if (aic->irq[irq].state == AIC_IRQ_ACTIVE) {
            aic->irq[irq].state = AIC_IRQ_INACTIVE;
        } else if (aic->irq[irq].state == AIC_IRQ_ACTIVE_PENDING) {
            aic->irq[irq].state = AIC_IRQ_PENDING;
        }
        aic->active_irq[cpu] = -1;
        aic_update(aic);
    }
}

int apple_aic_get_pending(apple_aic_state_t *aic, int cpu)
{
    if (cpu < 0 || cpu >= aic->num_cpus)
        return -1;
    return aic->pending_irq[cpu];
}

/* ── MMIO Handlers ──────────────────────────────── */

uint64_t apple_aic_mmio_read(apple_aic_state_t *aic, uint64_t offset, int size)
{
    (void)size;

    /* Global registers */
    switch (offset) {
    case AIC_REV:       return aic->rev;
    case AIC_CAP0:      return aic->cap0;
    case AIC_CAP1:      return aic->cap1;
    case AIC_GLB_CFG:   return aic->glb_cfg;

    case AIC_WHOAMI:    return 0; /* CPU 0 reads whoami */

    case AIC_IACK: {
        /* Global IACK — acknowledge on behalf of CPU 0 */
        return apple_aic_acknowledge(aic, 0);
    }
    }

    /* Per-CPU registers */
    for (int cpu = 0; cpu < aic->num_cpus; cpu++) {
        uint64_t base = AIC_CPU_BASE(cpu);
        if (offset >= base && offset < base + 0x80) {
            uint64_t reg = offset - base;
            switch (reg) {
            case 0x00: /* CPU_CTRL */
                return (aic->cpu[cpu].enabled ? AIC_CPU_CTRL_ENABLE : 0) |
                       (aic->cpu[cpu].mask_all ? AIC_CPU_CTRL_MASK_ALL : 0);
            case 0x04: /* CPU_IACK */
                return apple_aic_acknowledge(aic, cpu);
            case 0x10: /* CPU_IPI_MASK */
                return aic->cpu[cpu].ipi_mask;
            case 0x14: /* CPU_IPI_PEND */
                return aic->cpu[cpu].ipi_pending;
            default:
                return 0;
            }
        }
    }

    /* SPI mask/set registers */
    if (offset >= 0x4000 && offset < 0x5000) {
        int bank = (int)((offset - 0x4000) / 4);
        int irq_start = APPLE_AIC_IRQ_BASE + bank * 32;

        if (offset >= 0x4000 && offset < 0x4080) {
            /* MASK_SET — return current mask state */
            uint32_t mask = 0;
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                int irq = irq_start + i;
                if (aic->irq[irq].enabled && !aic->irq[irq].masked)
                    mask |= (1 << i);
            }
            return mask;
        }
        if (offset >= 0x4080 && offset < 0x4100) {
            /* MASK_CLR — return current mask state (inverted) */
            uint32_t mask = 0;
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                int irq = irq_start + i;
                if (aic->irq[irq].masked)
                    mask |= (1 << i);
            }
            return mask;
        }
        if (offset >= 0x4100 && offset < 0x4180) {
            /* SW_SET — return pending state */
            uint32_t pend = 0;
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                int irq = irq_start + i;
                if (aic->irq[irq].state == AIC_IRQ_PENDING ||
                    aic->irq[irq].state == AIC_IRQ_ACTIVE_PENDING)
                    pend |= (1 << i);
            }
            return pend;
        }
        if (offset >= 0x4180 && offset < 0x4200) {
            /* SW_CLR — return state (always 0 for this, readable as 0) */
            return 0;
        }
    }

    return 0;
}

void apple_aic_mmio_write(apple_aic_state_t *aic, uint64_t offset,
                           uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)(value & 0xFFFFFFFF);

    switch (offset) {
    case AIC_GLB_CFG:
        aic->glb_cfg = v & AIC_GLB_CFG_ENABLE;
        return;

    case AIC_IPI_SET: {
        /* Write CPU ID to IPI_SET to trigger IPI 0 on that CPU */
        int target = (int)(v & 0xFF);
        if (target < aic->num_cpus) {
            aic->cpu[target].ipi_pending |= (1 << 0);
            aic_signal_cpu(aic, target, APPLE_AIC_IRQ_BASE);
        }
        return;
    }

    case AIC_IPI_CLR: {
        int target = (int)(v & 0xFF);
        if (target < aic->num_cpus)
            aic->cpu[target].ipi_pending &= ~(1 << 0);
        return;
    }

    case AIC_RST:
        /* Soft reset — reinit state */
        apple_aic_init(aic, aic->num_cpus);
        return;
    }

    /* Per-CPU registers */
    for (int cpu = 0; cpu < aic->num_cpus; cpu++) {
        uint64_t base = AIC_CPU_BASE(cpu);
        if (offset >= base && offset < base + 0x80) {
            uint64_t reg = offset - base;
            switch (reg) {
            case 0x00: /* CPU_CTRL */
                aic->cpu[cpu].enabled = (v & AIC_CPU_CTRL_ENABLE) != 0;
                aic->cpu[cpu].mask_all = (v & AIC_CPU_CTRL_MASK_ALL) != 0;
                aic_update(aic);
                return;
            case 0x08: /* CPU_EOI */
                apple_aic_eoi(aic, cpu, (int)(v & 0xFFFFFFFF));
                return;
            case 0x10: /* CPU_IPI_MASK */
                aic->cpu[cpu].ipi_mask = v & 0xFF;
                return;
            }
            return;
        }
    }

    /* SPI mask set/clear registers */
    if (offset >= 0x4000 && offset < 0x5000) {
        int bank = (int)((offset - 0x4000) / 4);
        int irq_start = APPLE_AIC_IRQ_BASE + bank * 32;

        if (offset >= 0x4000 && offset < 0x4080) {
            /* MASK_SET — write 1 to enable IRQ */
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                if (v & (1 << i)) {
                    int irq = irq_start + i;
                    aic->irq[irq].enabled = true;
                    aic->irq[irq].masked = false;
                }
            }
            aic_update(aic);
            return;
        }
        if (offset >= 0x4080 && offset < 0x4100) {
            /* MASK_CLR — write 1 to disable IRQ */
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                if (v & (1 << i)) {
                    int irq = irq_start + i;
                    aic->irq[irq].masked = true;
                }
            }
            aic_update(aic);
            return;
        }
        if (offset >= 0x4100 && offset < 0x4180) {
            /* SW_SET — write 1 to set IRQ pending via software */
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                if (v & (1 << i)) {
                    apple_aic_raise_irq(aic, irq_start + i);
                }
            }
            return;
        }
        if (offset >= 0x4180 && offset < 0x4200) {
            /* SW_CLR — write 1 to clear IRQ */
            for (int i = 0; i < 32 && (irq_start + i) < APPLE_AIC_NUM_IRQ; i++) {
                if (v & (1 << i)) {
                    apple_aic_lower_irq(aic, irq_start + i);
                }
            }
            return;
        }
    }
}
