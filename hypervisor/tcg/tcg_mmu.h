/*
 * SiliconV — TCG MMU (Internal)
 *
 * ARMv8 stage-1 address translation for the TCG backend.
 * Supports 4KB granule, 39-bit and 48-bit virtual address spaces.
 */
#ifndef SILICONV_TCG_MMU_H
#define SILICONV_TCG_MMU_H

#include "tcg.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Translation result ──────────────────────────────── */
typedef struct {
    uint64_t phys_addr;   /* Translated physical address */
    int      is_mmio;     /* 1 if address is in MMIO space */
    int      fault;       /* Non-zero if translation faulted */
    int      fault_type;  /* Fault type: 0=none, 1=translation, 2=access */
} tcg_mmu_result_t;

/* ── Identity map configuration ──────────────────────── */
/*
 * Configure the software identity-map translator.
 *
 * This sets up a simple VA→PA translation formula used for initial boot:
 *   For kernel space VAs (0xFFFFxxxxxxxxxxxx):
 *     PA = VA - kernel_virt_base + kernel_phys_base
 *   For all other VAs:
 *     PA = VA (identity mapping)
 *
 * @param kernel_virt_base  Kernel linked virtual address (e.g., 0xFFFFFFF007004000)
 * @param kernel_phys_base  Kernel load physical address (e.g., 0x800001000)
 * @param ram_base          Guest RAM base address (e.g., 0x800000000)
 * @param ram_size          Size of guest RAM in bytes
 */
void tcg_mmu_set_identity_map(uint64_t kernel_virt_base,
                               uint64_t kernel_phys_base,
                               uint64_t ram_base,
                               uint64_t ram_size);

/* ── Address translation ────────────────────────────── */
/*
 * Perform ARMv8 stage-1 address translation.
 *
 * Translates a virtual address to a physical address using the page tables
 * pointed to by TTBR0_EL1/TTBR1_EL1.
 *
 * If SCTLR_EL1.M (bit 0) is 0 (MMU disabled), virt_addr is returned as-is.
 *
 * @param vcpu       vCPU state (provides TTBR0/TTBR1/TCR/SCTLR/MAIR)
 * @param virt_addr  Virtual address to translate
 * @param result     Output: translation result
 *
 * @return 0 on success, -1 on fault (result.fault set)
 */
int tcg_mmu_translate(tcg_vcpu_t *vcpu, uint64_t virt_addr,
                       tcg_mmu_result_t *result);

/* ── Resolve guest memory access (with translation) ──── */
/*
 * Resolves a guest memory access, performing virtual→physical translation
 * if the MMU is enabled.
 *
 * Returns 0 for RAM access (val populated for reads), 1 for MMIO, -1 for error.
 */
int tcg_mmu_resolve(tcg_vcpu_t *vcpu, uint64_t addr, int size,
                     uint64_t *val, bool is_write);

/* ── Read raw bytes from guest RAM (for instruction fetch) ── */
/*
 * Fetches instruction bytes from guest memory using virtual→physical
 * translation if the MMU is enabled.
 *
 * @param vcpu     vCPU state (for translation and VM access)
 * @param addr     Virtual address of instruction fetch
 * @param buf      Output buffer for fetched instruction
 * @param size     Number of bytes to fetch (typically 4)
 *
 * @return 0 on success, -1 on error
 */
int tcg_mmu_read(tcg_vcpu_t *vcpu, uint64_t addr, void *buf, int size);

#endif /* SILICONV_TCG_MMU_H */
