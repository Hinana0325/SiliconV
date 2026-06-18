/*
 * SiliconV — Apple DART (Device Address Resolution Table) Implementation
 *
 * Apple's IOMMU for device DMA address translation.
 * Supports up to 16 streams (SIDs), 4 TTBRs per stream,
 * and 3-level page table walks (L0 → L1 → L2 → 4KB page).
 *
 * For the virtual platform, DART is stub-capable:
 * can be configured in bypass mode (no translation) or
 * perform full page table walks when guest sets up page tables.
 */

#include "apple_dart.h"
#include <string.h>
#include <stdio.h>

void apple_dart_init(apple_dart_state_t *dart, int dart_id)
{
    memset(dart, 0, sizeof(*dart));

    dart->dart_id = dart_id;
    dart->params1 = APPLE_DART_MAX_STREAMS |
                    (APPLE_DART_TTBR_COUNT << 8) |
                    (0 << 16); /* 4KB pages */
    dart->params2 = 0;

    /* All streams disabled by default */
    for (int i = 0; i < APPLE_DART_MAX_STREAMS; i++) {
        dart->stream[i].enabled = false;
        dart->stream[i].bypass = true; /* Default to bypass */
        dart->stream[i].remap = i;
        for (int j = 0; j < APPLE_DART_TTBR_COUNT; j++) {
            dart->stream[i].ttbr[j] = 0;
        }
    }

    dart->error_status = DART_ERROR_NO_FAULT;
}

/* ── Page table walk ────────────────────────────── */
#define DART_PAGE_SHIFT     12  /* 4KB pages */
#define DART_PAGE_SIZE      (1UL << DART_PAGE_SHIFT)
#define DART_TABLE_MASK     (DART_PAGE_SIZE - 1)

/* Each level maps 8 bits (256 entries * 8 bytes = 2KB table) */
#define DART_L0_SHIFT       28
#define DART_L1_SHIFT       20
#define DART_L2_SHIFT       12

static uint64_t dart_pte_read(void *guest_ram, uint64_t ram_base,
                               uint64_t table_pa, int idx)
{
    uint64_t *pte_ptr = (uint64_t *)((uint8_t *)guest_ram +
                         (table_pa - ram_base) + idx * 8);
    return *pte_ptr;
}

static uint64_t dart_walk(apple_dart_state_t *dart, void *guest_ram,
                           uint64_t ram_base, uint64_t ttbr,
                           uint64_t iova, int *status)
{
    /* L0: bits [31:28] */
    int l0_idx = (int)((iova >> DART_L0_SHIFT) & 0xFF);
    uint64_t l0_pte = dart_pte_read(guest_ram, ram_base, ttbr, l0_idx);

    if (!(l0_pte & 1)) {
        *status = DART_ERROR_TRANSLATION_FAULT;
        return ~0ULL;
    }

    /* Check if L0 is a block mapping (for large pages) */
    if (l0_pte & 2) {
        /* Block mapping at L0: 256MB block */
        uint64_t block_base = l0_pte & ~((1ULL << DART_L0_SHIFT) - 1);
        uint64_t offset = iova & ((1ULL << DART_L0_SHIFT) - 1);
        return block_base | offset;
    }

    /* L1: bits [27:20] */
    uint64_t l1_base = l0_pte & ~0xFFFULL;
    int l1_idx = (int)((iova >> DART_L1_SHIFT) & 0xFF);
    uint64_t l1_pte = dart_pte_read(guest_ram, ram_base, l1_base, l1_idx);

    if (!(l1_pte & 1)) {
        *status = DART_ERROR_TRANSLATION_FAULT;
        return ~0ULL;
    }

    /* Check if L1 is a block mapping */
    if (l1_pte & 2) {
        /* Block mapping at L1: 1MB block */
        uint64_t block_base = l1_pte & ~((1ULL << DART_L1_SHIFT) - 1);
        uint64_t offset = iova & ((1ULL << DART_L1_SHIFT) - 1);
        return block_base | offset;
    }

    /* L2: bits [19:12] */
    uint64_t l2_base = l1_pte & ~0xFFFULL;
    int l2_idx = (int)((iova >> DART_L2_SHIFT) & 0xFF);
    uint64_t l2_pte = dart_pte_read(guest_ram, ram_base, l2_base, l2_idx);

    if (!(l2_pte & 1)) {
        *status = DART_ERROR_TRANSLATION_FAULT;
        return ~0ULL;
    }

    uint64_t page_base = l2_pte & ~0xFFFULL;
    uint64_t offset = iova & 0xFFFULL;
    return page_base | offset;
}

uint64_t apple_dart_translate(apple_dart_state_t *dart, int sid,
                               uint64_t iova, int size, bool write,
                               int *status)
{
    *status = DART_ERROR_NO_FAULT;

    if (sid < 0 || sid >= APPLE_DART_MAX_STREAMS) {
        *status = DART_ERROR_TRANSLATION_FAULT;
        return ~0ULL;
    }

    /* If stream is bypassed, IOVA = PA (identity mapping) */
    if (dart->stream[sid].bypass || !dart->stream[sid].enabled) {
        return iova;
    }

    /* If no TTBR configured, bypass */
    if (dart->stream[sid].ttbr[0] == 0) {
        return iova;
    }

    /* Full page table walk */
    /* Note: guest_ram base is context-dependent; callers provide translated RAM */
    /* This version assumes the caller has direct access to guest RAM */
    *status = DART_ERROR_TRANSLATION_FAULT; /* Stub: caller must provide RAM context */
    return ~0ULL;
}

void apple_dart_tlb_flush_all(apple_dart_state_t *dart)
{
    (void)dart;
    /* TLB flush — no-op for software-emulated DART */
}

void apple_dart_tlb_flush_sid(apple_dart_state_t *dart, int sid)
{
    (void)dart;
    (void)sid;
    /* TLB flush — no-op for software-emulated DART */
}

/* ── MMIO Read ──────────────────────────────────── */
uint64_t apple_dart_mmio_read(apple_dart_state_t *dart, uint64_t offset, int size)
{
    (void)size;

    switch (offset) {
    case DART_PARAMS1:      return dart->params1;
    case DART_PARAMS2:      return dart->params2;
    case DART_CONFIG:       return dart->config;
    case DART_ERROR_STATUS: return dart->error_status;
    case DART_ERROR_ADDR_HI: return (uint32_t)(dart->error_addr >> 32);
    case DART_ERROR_ADDR_LO: return (uint32_t)(dart->error_addr & 0xFFFFFFFF);
    case DART_TLB_OP:       return 0; /* Command register, read undefined */
    }

    /* SID_REMAP registers */
    if (offset >= 0x80 && offset < 0x80 + APPLE_DART_MAX_STREAMS * 4) {
        int sid = (int)((offset - 0x80) / 4);
        if (sid < APPLE_DART_MAX_STREAMS)
            return dart->stream[sid].remap;
        return 0;
    }

    /* SID_CONFIG registers */
    if (offset >= 0x100 && offset < 0x100 + APPLE_DART_MAX_STREAMS * 4) {
        int sid = (int)((offset - 0x100) / 4);
        if (sid < APPLE_DART_MAX_STREAMS) {
            uint32_t cfg = 0;
            if (dart->stream[sid].enabled) cfg |= DART_SID_CONFIG_ENABLE;
            if (dart->stream[sid].bypass)  cfg |= DART_SID_CONFIG_BYPASS;
            return cfg;
        }
        return 0;
    }

    /* TTBR registers */
    if (offset >= 0x200 && offset < 0x200 + APPLE_DART_MAX_STREAMS * 0x20) {
        int sid = (int)((offset - 0x200) / 0x20);
        int idx = (int)(((offset - 0x200) % 0x20) / 8);
        if (sid < APPLE_DART_MAX_STREAMS && idx < APPLE_DART_TTBR_COUNT) {
            return dart->stream[sid].ttbr[idx];
        }
        return 0;
    }

    return 0;
}

/* ── MMIO Write ─────────────────────────────────── */
void apple_dart_mmio_write(apple_dart_state_t *dart, uint64_t offset,
                            uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)(value & 0xFFFFFFFF);
    uint64_t v64 = value;

    switch (offset) {
    case DART_CONFIG:
        dart->config = v;
        return;

    case DART_TLB_OP:
        if (v == DART_TLB_OP_FLUSH_ALL) {
            apple_dart_tlb_flush_all(dart);
        } else if (v == DART_TLB_OP_FLUSH_SID) {
            /* SID is in upper bits */
            int sid = (int)((v >> 8) & 0xF);
            apple_dart_tlb_flush_sid(dart, sid);
        }
        return;
    }

    /* SID_REMAP registers */
    if (offset >= 0x80 && offset < 0x80 + APPLE_DART_MAX_STREAMS * 4) {
        int sid = (int)((offset - 0x80) / 4);
        if (sid < APPLE_DART_MAX_STREAMS) {
            dart->stream[sid].remap = v;
        }
        return;
    }

    /* SID_CONFIG registers */
    if (offset >= 0x100 && offset < 0x100 + APPLE_DART_MAX_STREAMS * 4) {
        int sid = (int)((offset - 0x100) / 4);
        if (sid < APPLE_DART_MAX_STREAMS) {
            dart->stream[sid].enabled = (v & DART_SID_CONFIG_ENABLE) != 0;
            dart->stream[sid].bypass  = (v & DART_SID_CONFIG_BYPASS) != 0;
        }
        return;
    }

    /* TTBR registers */
    if (offset >= 0x200 && offset < 0x200 + APPLE_DART_MAX_STREAMS * 0x20) {
        int sid = (int)((offset - 0x200) / 0x20);
        int idx = (int)(((offset - 0x200) % 0x20) / 8);
        if (sid < APPLE_DART_MAX_STREAMS && idx < APPLE_DART_TTBR_COUNT) {
            dart->stream[sid].ttbr[idx] = v64;
        }
        return;
    }
}
