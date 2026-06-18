/*
 * SiliconV — Apple DART (Device Address Resolution Table)
 *
 * Apple's IOMMU used in Apple Silicon SoCs for device DMA
 * address translation. Supports multiple streams (SIDs), TLB
 * operations, and 3-level page table walks.
 *
 * This device is needed for DMA-capable peripherals in the
 * Apple platform profile.
 */

#ifndef SILICONV_APPLE_DART_H
#define SILICONV_APPLE_DART_H

#include <stdint.h>
#include <stdbool.h>

/* ── Configuration ─────────────────────────────── */
#define APPLE_DART_MAX_STREAMS    16    /* Max SIDs */
#define APPLE_DART_TTBR_COUNT     4     /* Translation Table Base Registers */
#define APPLE_DART_L0_ENTRIES     256   /* L0: 8 bits */
#define APPLE_DART_L1_ENTRIES     256   /* L1: 8 bits */
#define APPLE_DART_L2_ENTRIES     256   /* L2: 8 bits → 4KB page */

/* ── Register Offsets ──────────────────────────── */
#define DART_PARAMS1         0x00    /* Parameters 1 */
#define DART_PARAMS2         0x04    /* Parameters 2 */
#define DART_TLB_OP          0x20    /* TLB Operation */
#define DART_ERROR_STATUS    0x40    /* Error status */
#define DART_ERROR_ADDR_HI   0x48    /* Error address high */
#define DART_ERROR_ADDR_LO   0x4C    /* Error address low */
#define DART_CONFIG          0x60    /* Configuration */
#define DART_SID_REMAP(n)    (0x80 + (n) * 4)     /* SID remap */
#define DART_SID_CONFIG(n)   (0x100 + (n) * 4)    /* SID config */
#define DART_TTBR(sid, idx)  (0x200 + (sid) * 0x20 + (idx) * 8) /* TTBR per SID */

/* ── PARAMS1 bits ──────────────────────────────── */
#define DART_PARAMS1_SID_COUNT_MASK  0x1F
#define DART_PARAMS1_TTBR_COUNT_MASK (0x7 << 8)
#define DART_PARAMS1_PAGE_SIZE       (1 << 16)   /* 0=4KB, 1=16KB */

/* ── SID_CONFIG bits ────────────────────────────── */
#define DART_SID_CONFIG_ENABLE      (1 << 0)
#define DART_SID_CONFIG_BYPASS     (1 << 1)    /* Bypass translation */
#define DART_SID_CONFIG_LOCK       (1 << 31)

/* ── TLB_OP commands ────────────────────────────── */
#define DART_TLB_OP_FLUSH_ALL      0x00000001
#define DART_TLB_OP_FLUSH_SID      0x00000002

/* ── Error status ──────────────────────────────── */
#define DART_ERROR_NO_FAULT         0
#define DART_ERROR_TRANSLATION_FAULT 1
#define DART_ERROR_PERMISSION_FAULT 2

/* ── Page table descriptor flags (L2) ──────────── */
#define DART_PTE_VALID      (1ULL << 0)
#define DART_PTE_WRITABLE   (1ULL << 1)
#define DART_PTE_READABLE   (1ULL << 2)

/* ── DART Instance State ────────────────────────── */
typedef struct {
    /* Identification */
    uint32_t dart_id;   /* Instance number (0 or 1) */

    /* Registers */
    uint32_t params1;
    uint32_t params2;
    uint32_t config;
    uint32_t error_status;
    uint64_t error_addr;

    /* Per-stream config */
    struct {
        bool     enabled;
        bool     bypass;
        uint32_t remap;          /* SID remap target */
        uint64_t ttbr[APPLE_DART_TTBR_COUNT]; /* Translation table base registers */
    } stream[APPLE_DART_MAX_STREAMS];

    /* MMIO region info */
    uint64_t mmio_base;
    uint64_t mmio_size;
} apple_dart_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize DART */
void apple_dart_init(apple_dart_state_t *dart, int dart_id);

/* Translate an IOVA to a physical address for a given SID
 * Returns the physical address, or ~0ULL on fault.
 * Sets *status to DART_ERROR_* on fault. */
uint64_t apple_dart_translate(apple_dart_state_t *dart, int sid,
                               uint64_t iova, int size, bool write,
                               int *status);

/* Flush all TLB entries */
void apple_dart_tlb_flush_all(apple_dart_state_t *dart);

/* Flush TLB for a specific SID */
void apple_dart_tlb_flush_sid(apple_dart_state_t *dart, int sid);

/* MMIO handlers */
uint64_t apple_dart_mmio_read(apple_dart_state_t *dart, uint64_t offset, int size);
void     apple_dart_mmio_write(apple_dart_state_t *dart, uint64_t offset,
                                uint64_t value, int size);

#endif /* SILICONV_APPLE_DART_H */
