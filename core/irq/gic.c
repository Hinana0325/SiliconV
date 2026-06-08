/*
 * SiliconV — GICv3 Emulation (Implementation)
 *
 * Distributor (GICD) handles SPI configuration and routing.
 * Redistributor (GICR) handles per-CPU SGI/PPI state.
 *
 * Priority-based preemption: lower number = higher priority.
 */

#include "gic.h"
#include <string.h>
#include <stdio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* ── Initialization ────────────────────────────── */

void gic_init(gic_state_t *gic, int num_cpus)
{
    memset(gic, 0, sizeof(*gic));
    gic->num_cpus = num_cpus;

    /* GICD_TYPER:
     *   [4:0]   ITLinesNumber = 31 (1024 IRQs / 32)
     *   [7:5]   CPUNumber = num_cpus - 1
     *   [9:8]   Reserved
     *   [10]    SecurityExtn = 1
     *   [11]    LSPI = 0 (no legacy)
     *   [15:13] MBIS = 0
     *   [23:16] IDbits = 0 (24-bit)
     *   [27:24] A3V = 0
     *   [31:28] No1N = 0
     */
    int num_irq_lines = (GIC_MAX_IRQ / 32) - 1;  /* 31 for 1024 IRQs */
    gic->gicd_typer = (num_irq_lines & 0x1f)
                    | (((num_cpus - 1) & 0x7) << 5)
                    | (1 << 10);  /* SecurityExtn */

    /* GICD_IIDR: Implementer = 0x43B (ARM), Revision = 1 */
    gic->gicd_iidr = (0x43B << 0) | (1 << 12) | (0x3 << 16);

    /* Initialize all IRQs */
    for (int i = 0; i < GIC_MAX_IRQ; i++) {
        gic->irq[i].state = GIC_IRQ_INACTIVE;
        gic->irq[i].priority = 0xA0;  /* Default priority */
        gic->irq[i].enabled = false;
        gic->irq[i].targets = 0;
        gic->irq[i].group = 0;
        gic->irq[i].config = 0;  /* Level-triggered */
        gic->irq[i].route = 0;

        /* SGI: edge-triggered, always enabled */
        if (i < GIC_SGI_COUNT) {
            gic->irq[i].config = 1;  /* Edge */
            gic->irq[i].enabled = true;
            gic->irq[i].targets = 0xFF;  /* All CPUs */
        }
        /* PPI: level-triggered */
        else if (i < GIC_SPI_BASE) {
            gic->irq[i].config = 0;  /* Level */
        }
    }

    /* Initialize redistributors */
    for (int i = 0; i < num_cpus; i++) {
        gic_redist_t *r = &gic->redist[i];
        r->ctlr = 0;
        r->waker = GICR_WAKER_CA | GICR_WAKER_PS;  /* CPU asleep */
        r->gicr_ipriorityr[0] = 0xA0;  /* Default PPI/SGI priority */
        /* All SGI/PPI enabled by default */
        r->gicr_isenabler0 = 0xFFFFFFFF;
    }
}

void gic_set_callback(gic_state_t *gic,
                      void (*cb)(int cpu, int irq, void *opaque),
                      void *opaque)
{
    gic->irq_callback = cb;
    gic->irq_opaque = opaque;
}

/* ── Interrupt Injection ───────────────────────── */

void gic_raise_spi(gic_state_t *gic, int irq)
{
    if (irq < GIC_SPI_BASE || irq >= GIC_MAX_IRQ)
        return;

    gic_irq_t *p = &gic->irq[irq];
    if (!p->enabled)
        return;

    p->state = GIC_IRQ_PENDING;

    /* Deliver to target CPUs */
    uint8_t targets = p->targets;
    if (gic->gicd_ctlr & GICD_CTLR_ARE_NS) {
        /* Affinity routing: use IROUTER */
        /* Simplified: route to CPU 0 if no affinity set */
        targets = (p->route == 0) ? 0x1 : (1 << (p->route & 0x7));
    }

    for (int cpu = 0; cpu < gic->num_cpus; cpu++) {
        if (targets & (1 << cpu)) {
            if (gic->irq_callback)
                gic->irq_callback(cpu, irq, gic->irq_opaque);
        }
    }
}

void gic_lower_spi(gic_state_t *gic, int irq)
{
    if (irq < GIC_SPI_BASE || irq >= GIC_MAX_IRQ)
        return;
    gic->irq[irq].state = GIC_IRQ_INACTIVE;
}

void gic_raise_sgi(gic_state_t *gic, int irq, int target_cpu)
{
    if (irq >= GIC_SGI_COUNT || target_cpu >= gic->num_cpus)
        return;

    gic_irq_t *p = &gic->irq[irq];
    p->state = GIC_IRQ_PENDING;

    if (gic->irq_callback)
        gic->irq_callback(target_cpu, irq, gic->irq_opaque);
}

/* ── Interrupt Acknowledge / EOI ───────────────── */

int gic_acknowledge_irq(gic_state_t *gic, int cpu)
{
    /* Find highest priority pending IRQ for this CPU */
    int best_irq = 1023;  /* Spurious */
    uint8_t best_prio = 0xFF;

    /* Check SGI/PPI first (per-CPU) */
    gic_redist_t *r = &gic->redist[cpu];
    for (int i = 0; i < GIC_SPI_BASE; i++) {
        gic_irq_t *p = &gic->irq[i];
        if (p->state == GIC_IRQ_PENDING && p->enabled) {
            if (p->priority < best_prio) {
                best_prio = p->priority;
                best_irq = i;
            }
        }
    }

    /* Check SPI */
    for (int i = GIC_SPI_BASE; i < GIC_MAX_IRQ; i++) {
        gic_irq_t *p = &gic->irq[i];
        if (p->state == GIC_IRQ_PENDING && p->enabled) {
            uint8_t targets = p->targets;
            if (gic->gicd_ctlr & GICD_CTLR_ARE_NS) {
                targets = (p->route == 0) ? 0x1 : (1 << (p->route & 0x7));
            }
            if ((targets & (1 << cpu)) && p->priority < best_prio) {
                best_prio = p->priority;
                best_irq = i;
            }
        }
    }

    if (best_irq != 1023) {
        /* Transition to active */
        gic_irq_t *p = &gic->irq[best_irq];
        if (p->state == GIC_IRQ_PENDING)
            p->state = GIC_IRQ_ACTIVE;
        else if (p->state == GIC_IRQ_ACTIVE_PENDING)
            p->state = GIC_IRQ_ACTIVE;
    }

    return best_irq;
}

void gic_eoi(gic_state_t *gic, int cpu, int irq)
{
    (void)cpu;
    if (irq >= GIC_MAX_IRQ)
        return;

    gic_irq_t *p = &gic->irq[irq];
    if (p->state == GIC_IRQ_ACTIVE)
        p->state = GIC_IRQ_INACTIVE;
    else if (p->state == GIC_IRQ_ACTIVE_PENDING)
        p->state = GIC_IRQ_PENDING;
}

int gic_get_pending_irq(gic_state_t *gic, int cpu)
{
    return gic_acknowledge_irq(gic, cpu);
}

/* ── GICD MMIO ─────────────────────────────────── */

uint64_t gicd_mmio_read(gic_state_t *gic, uint64_t offset, int size)
{
    (void)size;

    /* Word-aligned reads */
    if (offset >= GICD_IROUTER(0) && offset < GICD_IROUTER(0) + GIC_MAX_IRQ * 8) {
        int idx = (offset - GICD_IROUTER(0)) / 8;
        return gic->irouter[idx];
    }

    switch (offset) {
    case GICD_CTLR:    return gic->gicd_ctlr;
    case GICD_TYPER:   return gic->gicd_typer;
    case GICD_IIDR:    return gic->gicd_iidr;
    case GICD_STATUSR: return gic->gicd_statusr;
    }

    /* IGROUPR: 32 IRQs per register */
    if (offset >= GICD_IGROUPR(0) && offset < GICD_IGROUPR(32)) {
        int reg = (offset - GICD_IGROUPR(0)) / 4;
        uint32_t val = 0;
        for (int i = 0; i < 32; i++) {
            if (gic->irq[reg * 32 + i].group)
                val |= (1 << i);
        }
        return val;
    }

    /* ISENABLER */
    if (offset >= GICD_ISENABLER(0) && offset < GICD_ISENABLER(32)) {
        int reg = (offset - GICD_ISENABLER(0)) / 4;
        uint32_t val = 0;
        for (int i = 0; i < 32; i++) {
            if (gic->irq[reg * 32 + i].enabled)
                val |= (1 << i);
        }
        return val;
    }

    /* ICENABLER — read returns same as ISENABLER */
    if (offset >= GICD_ICENABLER(0) && offset < GICD_ICENABLER(32)) {
        int reg = (offset - GICD_ICENABLER(0)) / 4;
        uint32_t val = 0;
        for (int i = 0; i < 32; i++) {
            if (gic->irq[reg * 32 + i].enabled)
                val |= (1 << i);
        }
        return val;
    }

    /* ISPENDR */
    if (offset >= GICD_ISPENDR(0) && offset < GICD_ISPENDR(32)) {
        int reg = (offset - GICD_ISPENDR(0)) / 4;
        uint32_t val = 0;
        for (int i = 0; i < 32; i++) {
            gic_irq_state_t s = gic->irq[reg * 32 + i].state;
            if (s == GIC_IRQ_PENDING || s == GIC_IRQ_ACTIVE_PENDING)
                val |= (1 << i);
        }
        return val;
    }

    /* ICPENDR — same as ISPENDR on read */
    if (offset >= GICD_ICPENDR(0) && offset < GICD_ICPENDR(32)) {
        int reg = (offset - GICD_ICPENDR(0)) / 4;
        uint32_t val = 0;
        for (int i = 0; i < 32; i++) {
            gic_irq_state_t s = gic->irq[reg * 32 + i].state;
            if (s == GIC_IRQ_PENDING || s == GIC_IRQ_ACTIVE_PENDING)
                val |= (1 << i);
        }
        return val;
    }

    /* ISACTIVER */
    if (offset >= GICD_ISACTIVER(0) && offset < GICD_ISACTIVER(32)) {
        int reg = (offset - GICD_ISACTIVER(0)) / 4;
        uint32_t val = 0;
        for (int i = 0; i < 32; i++) {
            gic_irq_state_t s = gic->irq[reg * 32 + i].state;
            if (s == GIC_IRQ_ACTIVE || s == GIC_IRQ_ACTIVE_PENDING)
                val |= (1 << i);
        }
        return val;
    }

    /* IPRIORITYR: 4 IRQs per register */
    if (offset >= GICD_IPRIORITYR(0) && offset < GICD_IPRIORITYR(GIC_MAX_IRQ)) {
        int reg = (offset - GICD_IPRIORITYR(0)) / 4;
        int base_irq = reg * 4;
        uint32_t val = 0;
        for (int i = 0; i < 4 && (base_irq + i) < GIC_MAX_IRQ; i++) {
            val |= (uint32_t)gic->irq[base_irq + i].priority << (i * 8);
        }
        return val;
    }

    /* ITARGETSR: 4 IRQs per register, byte-accessible */
    if (offset >= GICD_ITARGETSR(0) && offset < GICD_ITARGETSR(GIC_MAX_IRQ)) {
        int reg = (offset - GICD_ITARGETSR(0)) / 4;
        int base_irq = reg * 4;
        uint32_t val = 0;
        for (int i = 0; i < 4 && (base_irq + i) < GIC_MAX_IRQ; i++) {
            val |= (uint32_t)gic->irq[base_irq + i].targets << (i * 8);
        }
        return val;
    }

    /* ICFGR: 16 IRQs per register (2 bits each) */
    if (offset >= GICD_ICFGR(0) && offset < GICD_ICFGR(GIC_MAX_IRQ / 16)) {
        int reg = (offset - GICD_ICFGR(0)) / 4;
        int base_irq = reg * 16;
        uint32_t val = 0;
        for (int i = 0; i < 16 && (base_irq + i) < GIC_MAX_IRQ; i++) {
            val |= (uint32_t)gic->irq[base_irq + i].config << (i * 2 + 1);
        }
        return val;
    }

    fprintf(stderr, "gicd: unhandled read offset=0x%lx\n", offset);
    return 0;
}

void gicd_mmio_write(gic_state_t *gic, uint64_t offset, uint64_t value, int size)
{
    (void)size;

    /* IROUTER */
    if (offset >= GICD_IROUTER(0) && offset < GICD_IROUTER(0) + GIC_MAX_IRQ * 8) {
        int idx = (offset - GICD_IROUTER(0)) / 8;
        gic->irouter[idx] = value & 0xFF000000FFULL;
        gic->irq[idx].route = value;
        return;
    }

    switch (offset) {
    case GICD_CTLR: {
        gic->gicd_ctlr = value & 0x7F;
        return;
    }
    case GICD_STATUSR:
        gic->gicd_statusr = 0;  /* Write-to-clear */
        return;
    }

    /* IGROUPR */
    if (offset >= GICD_IGROUPR(0) && offset < GICD_IGROUPR(32)) {
        int reg = (offset - GICD_IGROUPR(0)) / 4;
        for (int i = 0; i < 32; i++) {
            int irq = reg * 32 + i;
            if (irq < GIC_MAX_IRQ)
                gic->irq[irq].group = (value & (1 << i)) ? 1 : 0;
        }
        return;
    }

    /* ISENABLER — set enable bits */
    if (offset >= GICD_ISENABLER(0) && offset < GICD_ISENABLER(32)) {
        int reg = (offset - GICD_ISENABLER(0)) / 4;
        for (int i = 0; i < 32; i++) {
            int irq = reg * 32 + i;
            if ((value & (1 << i)) && irq < GIC_MAX_IRQ) {
                gic->irq[irq].enabled = true;
            }
        }
        return;
    }

    /* ICENABLER — clear enable bits */
    if (offset >= GICD_ICENABLER(0) && offset < GICD_ICENABLER(32)) {
        int reg = (offset - GICD_ICENABLER(0)) / 4;
        for (int i = 0; i < 32; i++) {
            int irq = reg * 32 + i;
            if ((value & (1 << i)) && irq < GIC_MAX_IRQ) {
                gic->irq[irq].enabled = false;
            }
        }
        return;
    }

    /* ISPENDR — set pending */
    if (offset >= GICD_ISPENDR(0) && offset < GICD_ISPENDR(32)) {
        int reg = (offset - GICD_ISPENDR(0)) / 4;
        for (int i = 0; i < 32; i++) {
            int irq = reg * 32 + i;
            if ((value & (1 << i)) && irq < GIC_MAX_IRQ) {
                if (gic->irq[irq].state == GIC_IRQ_INACTIVE)
                    gic->irq[irq].state = GIC_IRQ_PENDING;
                else if (gic->irq[irq].state == GIC_IRQ_ACTIVE)
                    gic->irq[irq].state = GIC_IRQ_ACTIVE_PENDING;
            }
        }
        return;
    }

    /* ICPENDR — clear pending */
    if (offset >= GICD_ICPENDR(0) && offset < GICD_ICPENDR(32)) {
        int reg = (offset - GICD_ICPENDR(0)) / 4;
        for (int i = 0; i < 32; i++) {
            int irq = reg * 32 + i;
            if ((value & (1 << i)) && irq < GIC_MAX_IRQ) {
                if (gic->irq[irq].state == GIC_IRQ_PENDING)
                    gic->irq[irq].state = GIC_IRQ_INACTIVE;
                else if (gic->irq[irq].state == GIC_IRQ_ACTIVE_PENDING)
                    gic->irq[irq].state = GIC_IRQ_ACTIVE;
            }
        }
        return;
    }

    /* ISACTIVER — set active */
    if (offset >= GICD_ISACTIVER(0) && offset < GICD_ISACTIVER(32)) {
        int reg = (offset - GICD_ISACTIVER(0)) / 4;
        for (int i = 0; i < 32; i++) {
            int irq = reg * 32 + i;
            if ((value & (1 << i)) && irq < GIC_MAX_IRQ) {
                if (gic->irq[irq].state == GIC_IRQ_INACTIVE)
                    gic->irq[irq].state = GIC_IRQ_ACTIVE;
                else if (gic->irq[irq].state == GIC_IRQ_PENDING)
                    gic->irq[irq].state = GIC_IRQ_ACTIVE_PENDING;
            }
        }
        return;
    }

    /* IPRIORITYR */
    if (offset >= GICD_IPRIORITYR(0) && offset < GICD_IPRIORITYR(GIC_MAX_IRQ)) {
        int reg = (offset - GICD_IPRIORITYR(0)) / 4;
        int base_irq = reg * 4;
        for (int i = 0; i < 4 && (base_irq + i) < GIC_MAX_IRQ; i++) {
            gic->irq[base_irq + i].priority = (value >> (i * 8)) & 0xFF;
        }
        return;
    }

    /* ITARGETSR */
    if (offset >= GICD_ITARGETSR(0) && offset < GICD_ITARGETSR(GIC_MAX_IRQ)) {
        int reg = (offset - GICD_ITARGETSR(0)) / 4;
        int base_irq = reg * 4;
        for (int i = 0; i < 4 && (base_irq + i) < GIC_MAX_IRQ; i++) {
            gic->irq[base_irq + i].targets = (value >> (i * 8)) & 0xFF;
        }
        return;
    }

    /* ICFGR */
    if (offset >= GICD_ICFGR(0) && offset < GICD_ICFGR(GIC_MAX_IRQ / 16)) {
        int reg = (offset - GICD_ICFGR(0)) / 4;
        int base_irq = reg * 16;
        for (int i = 0; i < 16 && (base_irq + i) < GIC_MAX_IRQ; i++) {
            gic->irq[base_irq + i].config = (value >> (i * 2 + 1)) & 1;
        }
        return;
    }

    /* SGIR — Software Generated Interrupt Register */
    if (offset == GICD_SGIR) {
        int sgi_id = value & 0xF;
        int target_list = (value >> 16) & 0xFF;
        int cpu;
        for (cpu = 0; cpu < gic->num_cpus; cpu++) {
            if (target_list & (1 << cpu))
                gic_raise_sgi(gic, sgi_id, cpu);
        }
        return;
    }

    fprintf(stderr, "gicd: unhandled write offset=0x%lx val=0x%lx\n", offset, value);
}

/* ── GICR MMIO ─────────────────────────────────── */

uint64_t gicr_mmio_read(gic_state_t *gic, int cpu, uint64_t offset, int size)
{
    (void)size;
    gic_redist_t *r = &gic->redist[cpu];

    /* RD_base frame (0x0000 - 0x0FFFF) */
    switch (offset) {
    case GICR_CTLR:   return r->ctlr;
    case GICR_IIDR:   return (0x43B << 0) | (1 << 12) | (0x3 << 16);
    case GICR_TYPER: {
        /* TYPER: affinity = cpu, Last = (cpu == num_cpus-1) */
        uint64_t typer = ((uint64_t)cpu << 8);  /* Affinity Value */
        if (cpu == gic->num_cpus - 1)
            typer |= GICR_TYPER_LAST;
        return typer;
    }
    case GICR_STATUSR: return 0;
    case GICR_WAKER:   return r->waker;
    }

    /* SGI/PPI frame (0x10000 - 0x1FFFF) */
    if (offset >= GICR_SGI_BASE) {
        uint64_t sgi_off = offset - GICR_SGI_BASE;

        switch (sgi_off + GICR_SGI_BASE) {
        case GICR_IGROUPR0:     return r->gicr_igroupr0;
        case GICR_ISENABLER0:   return r->gicr_isenabler0;
        case GICR_ICENABLER0:   return r->gicr_isenabler0;  /* Read = enable state */
        case GICR_ISPENDR0:     return r->gicr_ispendr0;
        case GICR_ICPENDR0:     return r->gicr_ispendr0;
        case GICR_ISACTIVER0:   return r->gicr_isactiver0;
        case GICR_ICACTIVER0:   return r->gicr_isactiver0;
        case GICR_ICFGR0:       return r->gicr_icfgr[0];
        case GICR_ICFGR1:       return r->gicr_icfgr[1];
        case GICR_IGRPMODR0:    return r->gicr_igrpmodr0;
        case GICR_NSACR:        return r->gicr_nsacr;
        }

        /* IPRIORITYR */
        if (offset >= GICR_IPRIORITYR(0) && offset < GICR_IPRIORITYR(8)) {
            int reg = (offset - GICR_IPRIORITYR(0)) / 4;
            uint32_t val = 0;
            for (int i = 0; i < 4; i++) {
                val |= (uint32_t)r->gicr_ipriorityr[reg * 4 + i] << (i * 8);
            }
            return val;
        }
    }

    fprintf(stderr, "gicr[%d]: unhandled read offset=0x%lx\n", cpu, offset);
    return 0;
}

void gicr_mmio_write(gic_state_t *gic, int cpu, uint64_t offset,
                     uint64_t value, int size)
{
    (void)size;
    gic_redist_t *r = &gic->redist[cpu];

    /* RD_base frame */
    switch (offset) {
    case GICR_CTLR:
        r->ctlr = value & 0x1;
        return;
    case GICR_STATUSR:
        r->gicr_ipriorityr[0] = 0;  /* Write-to-clear */
        return;
    case GICR_WAKER:
        r->waker = value & 0x6;
        return;
    }

    /* SGI/PPI frame */
    if (offset >= GICR_SGI_BASE) {
        switch (offset) {
        case GICR_IGROUPR0:
            r->gicr_igroupr0 = value;
            return;
        case GICR_ISENABLER0:
            r->gicr_isenabler0 |= value;
            return;
        case GICR_ICENABLER0:
            r->gicr_isenabler0 &= ~value;
            return;
        case GICR_ISPENDR0:
            r->gicr_ispendr0 |= value;
            /* Also set IRQ state for SGI/PPI */
            for (int i = 0; i < 32; i++) {
                if (value & (1 << i)) {
                    if (gic->irq[i].state == GIC_IRQ_INACTIVE)
                        gic->irq[i].state = GIC_IRQ_PENDING;
                    else if (gic->irq[i].state == GIC_IRQ_ACTIVE)
                        gic->irq[i].state = GIC_IRQ_ACTIVE_PENDING;
                }
            }
            return;
        case GICR_ICPENDR0:
            r->gicr_ispendr0 &= ~value;
            for (int i = 0; i < 32; i++) {
                if (value & (1 << i)) {
                    if (gic->irq[i].state == GIC_IRQ_PENDING)
                        gic->irq[i].state = GIC_IRQ_INACTIVE;
                    else if (gic->irq[i].state == GIC_IRQ_ACTIVE_PENDING)
                        gic->irq[i].state = GIC_IRQ_ACTIVE;
                }
            }
            return;
        case GICR_ISACTIVER0:
            r->gicr_isactiver0 |= value;
            return;
        case GICR_ICACTIVER0:
            r->gicr_isactiver0 &= ~value;
            return;
        case GICR_ICFGR0:
            r->gicr_icfgr[0] = value;
            return;
        case GICR_ICFGR1:
            r->gicr_icfgr[1] = value;
            return;
        case GICR_IGRPMODR0:
            r->gicr_igrpmodr0 = value;
            return;
        case GICR_NSACR:
            r->gicr_nsacr = value;
            return;
        }

        /* IPRIORITYR */
        if (offset >= GICR_IPRIORITYR(0) && offset < GICR_IPRIORITYR(8)) {
            int reg = (offset - GICR_IPRIORITYR(0)) / 4;
            for (int i = 0; i < 4; i++) {
                r->gicr_ipriorityr[reg * 4 + i] = (value >> (i * 8)) & 0xFF;
            }
            return;
        }
    }

    fprintf(stderr, "gicr[%d]: unhandled write offset=0x%lx val=0x%lx\n",
            cpu, offset, value);
}
