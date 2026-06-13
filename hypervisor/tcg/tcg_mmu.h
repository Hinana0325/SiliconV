/*
 * SiliconV — TCG MMU (Internal)
 */
#ifndef SILICONV_TCG_MMU_H
#define SILICONV_TCG_MMU_H

#include "tcg.h"

/* Resolve guest memory access.
 * Returns 0 for RAM, 1 for MMIO, -1 for error. */
int tcg_mmu_resolve(tcg_vcpu_t *vcpu, uint64_t addr, int size,
                     uint64_t *val, bool is_write);

/* Read raw bytes from guest RAM (for instruction fetch) */
int tcg_mmu_read(tcg_vm_t *vm, uint64_t addr, void *buf, int size);

#endif /* SILICONV_TCG_MMU_H */
