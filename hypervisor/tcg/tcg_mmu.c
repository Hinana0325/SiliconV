/*
 * SiliconV — TCG MMU (Memory Management Unit)
 *
 * Implements ARMv8 stage-1 address translation for the TCG backend.
 *
 * APPROACH:
 * For initial XNU boot, we DON'T create real hardware page tables.
 * Instead, we use a software translation function that applies a
 * fixed formula:
 *
 *   For kernel space VAs (TTBR1, bits[63:48] = 0xFFFF):
 *     PA = VA - kernel_virt_base + kernel_phys_base
 *     (This maps kernel's linked VA addresses to the physical RAM)
 *
 *   For all other VAs (identity/physical space):
 *     PA = VA  (identity mapping)
 *
 * This matches what iBoot's page tables would provide: an identity
 * mapping of physical memory in TTBR0, and a linear mapping of the
 * kernel in TTBR1.
 *
 * When the kernel later sets up its own page tables (by writing to
 * TTBR0/TTBR1), we'll switch to real page table walking.
 */

#include "tcg.h"
#include "tcg_cpu.h"
#include "tcg_mmu.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Global state for software MMU translation ──────────── */
/* These are set up by machine_apple.c before booting the kernel */
static uint64_t g_kernel_virt_base = 0;
static uint64_t g_kernel_phys_base = 0;
static uint64_t g_ram_base = 0;
static uint64_t g_ram_size = 0;

void tcg_mmu_set_identity_map(uint64_t kernel_virt_base,
                               uint64_t kernel_phys_base,
                               uint64_t ram_base,
                               uint64_t ram_size)
{
    g_kernel_virt_base = kernel_virt_base;
    g_kernel_phys_base = kernel_phys_base;
    g_ram_base = ram_base;
    g_ram_size = ram_size;

    printf("tcg_mmu: identity map configured:\n");
    printf("  kernel VA=0x%lx → PA=0x%lx\n",
           (unsigned long)kernel_virt_base,
           (unsigned long)kernel_phys_base);
    printf("  RAM: 0x%lx .. 0x%lx\n",
           (unsigned long)ram_base,
           (unsigned long)(ram_base + ram_size));
}

/* ── Software virtual→physical translation ─────────────── */
/*
 * Translates a VA to PA using the identity map offset formula.
 * This is used when MMU is disabled (SCTLR_EL1.M == 0) or as
 * a fallback for simple boot scenarios.
 *
 * For kernel space (bit[47:48...63] = 0xFFFF...): apply offset
 * For everything else: identity (PA = VA)
 */
static int translate_identity(uint64_t virt_addr, uint64_t *phys_addr)
{
    /* Check if this is a kernel virtual address (TTBR1 space).
     * With 48-bit VA, TTBR1 space starts at 0xFFFF000000000000 */
    if ((virt_addr >> 48) == 0xFFFFULL) {
        /* Kernel space: apply linear offset */
        uint64_t offset_from_base = virt_addr - g_kernel_virt_base;
        /* Handle the case where the VA is below kernel_virt_base but still in kernel space */
        if (virt_addr >= g_kernel_virt_base) {
            *phys_addr = g_kernel_phys_base + offset_from_base;
        } else {
            /* VA is in TTBR1 space but below the kernel's linked address.
             * This might be BootArgs or other data mapped at a different offset.
             * Use identity mapping as fallback. */
            *phys_addr = virt_addr;
        }
    } else {
        /* Identity mapping for all other addresses */
        *phys_addr = virt_addr;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 *  Public: MMU Address Translation
 * ══════════════════════════════════════════════════════════════ */

int tcg_mmu_translate(tcg_vcpu_t *vcpu, uint64_t virt_addr,
                       tcg_mmu_result_t *result)
{
    memset(result, 0, sizeof(*result));

    /* If MMU is enabled, check for real page tables */
    if (vcpu->sctlr_el1 & 1) {
        /* Check if the kernel has set up its own page tables */
        uint64_t ttbr0 = vcpu->ttbr0_el1;
        uint64_t ttbr1 = vcpu->ttbr1_el1;

        /*
         * If TTBR0/TTBR1 point to non-zero addresses, the kernel has set up
         * its own page tables. In that case, we would need to walk them.
         * For now, always use the identity/offset translation.
         *
         * FIXME: When the kernel sets TTBR0/TTBR1 to non-zero values pointing
         * to its own page tables, we should walk them instead of using the
         * identity formula. For initial boot, the offset formula suffices.
         */
        (void)ttbr0;
        (void)ttbr1;
    }

    /* Use software identity translation */
    if (g_kernel_virt_base != 0) {
        translate_identity(virt_addr, &result->phys_addr);
    } else {
        /* No identity map configured — use bare address */
        result->phys_addr = virt_addr;
    }

    /* Determine if the result is RAM or MMIO */
    result->is_mmio = (result->phys_addr < g_ram_base ||
                      result->phys_addr >= g_ram_base + g_ram_size);

    return 0;
}

/* ══════════════════════════════════════════════════════════════
 *  Public: MMU Resolve (data access with translation)
 * ══════════════════════════════════════════════════════════════ */

int tcg_mmu_resolve(tcg_vcpu_t *vcpu, uint64_t addr, int size,
                     uint64_t *val, bool is_write)
{
    tcg_vm_t *vm = vcpu->vm;

    /* Translate virtual address if MMU is enabled */
    uint64_t phys_addr = addr;
    if (vcpu->sctlr_el1 & 1) {
        tcg_mmu_result_t tr;
        if (tcg_mmu_translate(vcpu, addr, &tr) < 0 || tr.fault) {
            fprintf(stderr, "tcg: MMU translation fault at VA=0x%lx (PC=0x%lx, insn=%lu)\n",
                    (unsigned long)addr, (unsigned long)(vcpu->pc),
                    (unsigned long)vcpu->insn_count);
            return -1;
        }
        phys_addr = tr.phys_addr;
    }

    /* Determine if this is MMIO */
    uint64_t ram_base = vm->config.ram_base;
    uint64_t ram_end  = ram_base + vm->ram_size;

    if (phys_addr < ram_base || phys_addr + (uint64_t)size > ram_end) {
        /* MMIO access — call through to machine.c dispatch */
        if (is_write) {
            vm->mmio_write(vm->callback_opaque, phys_addr, *val, size);
        } else {
            *val = vm->mmio_read(vm->callback_opaque, phys_addr, size);
        }
        return 1; /* MMIO */
    }

    /* RAM access */
    uint64_t offset = phys_addr - ram_base;

    if (is_write) {
        switch (size) {
        case 1: vm->ram[offset] = (uint8_t)*val; break;
        case 2: *(uint16_t *)(vm->ram + offset) = (uint16_t)*val; break;
        case 4: *(uint32_t *)(vm->ram + offset) = (uint32_t)*val; break;
        case 8: *(uint64_t *)(vm->ram + offset) = *val; break;
        default: return -1;
        }
    } else {
        switch (size) {
        case 1: *val = vm->ram[offset]; break;
        case 2: *val = *(uint16_t *)(vm->ram + offset); break;
        case 4: *val = *(uint32_t *)(vm->ram + offset); break;
        case 8: *val = *(uint64_t *)(vm->ram + offset); break;
        default: return -1;
        }
    }

    return 0; /* RAM access */
}

/* ══════════════════════════════════════════════════════════════
 *  Public: MMU Read (instruction fetch with translation)
 * ══════════════════════════════════════════════════════════════ */

int tcg_mmu_read(tcg_vcpu_t *vcpu, uint64_t virt_addr, void *buf, int size)
{
    tcg_vm_t *vm = vcpu->vm;

    /* Translate virtual address if MMU is enabled */
    uint64_t phys_addr = virt_addr;
    if (vcpu->sctlr_el1 & 1) {
        tcg_mmu_result_t tr;
        if (tcg_mmu_translate(vcpu, virt_addr, &tr) < 0 || tr.fault) {
            fprintf(stderr, "tcg: instruction fetch translation fault at VA=0x%lx (PC=0x%lx)\n",
                    (unsigned long)virt_addr, (unsigned long)(vcpu->pc));
            return -1;
        }
        phys_addr = tr.phys_addr;

        if (tr.is_mmio) {
            fprintf(stderr, "tcg: instruction fetch from MMIO at PA=0x%lx\n",
                    (unsigned long)phys_addr);
            return -1;
        }
    }

    /* Read from guest RAM */
    uint64_t offset = phys_addr - vm->config.ram_base;
    if (offset + (uint64_t)size > vm->ram_size) {
        fprintf(stderr, "tcg: instruction fetch from non-RAM at PA=0x%lx\n",
                (unsigned long)phys_addr);
        return -1;
    }

    memcpy(buf, vm->ram + offset, size);
    return 0;
}
