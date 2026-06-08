/*
 * SiliconV — ION / DMABUF Heap Interface
 *
 * ION is Android's memory allocator for DMA buffers.
 * Modern Android (5.10+) uses DMABUF heaps instead.
 *
 * SiliconV supports both:
 *   - Legacy: ION (CONFIG_ION)
 *   - Modern: DMABUF heaps (CONFIG_DMABUF_HEAPS)
 *
 * For GPU, camera, video, and display buffers.
 *
 * Reference: drivers/dma-buf/heaps/
 */

#ifndef SILICONV_DMABUF_H
#define SILICONV_DMABUF_H

#include <stdint.h>

/* ── ION Heap IDs ──────────────────────────────── */
#define ION_HEAP_SYSTEM         0
#define ION_HEAP_SYSTEM_CONTIG  1
#define ION_HEAP_CARVEOUT       2
#define ION_HEAP_TYPE_DMA       3

/* ION flags */
#define ION_FLAG_CACHED         (1 << 0)
#define ION_FLAG_CACHED_NEEDS_SYNC (1 << 1)

/* ── ION ioctl ─────────────────────────────────── */
struct ion_allocation_data {
    uint64_t len;
    uint64_t heap_id_mask;
    uint32_t flags;
    int      fd;           /* Output: dmabuf fd */
};

struct ion_heap_data {
    char     name[32];
    uint32_t type;
    uint32_t heap_id;
    uint32_t size;         /* Heap size in bytes */
};

#define ION_IOC_ALLOC    _IOWR('I', 0, struct ion_allocation_data)
#define ION_IOC_HEAP_QUERY _IOWR('I', 8, struct ion_heap_query)

/* ── DMABUF Heap Names (Modern) ────────────────── */
#define DMABUF_HEAP_SYSTEM    "system"
#define DMABUF_HEAP_CMA       "reserved"  /* CMA heap */

/* ── SiliconV DMA Heap Configuration ───────────── */
typedef struct {
    bool use_ion;           /* true = legacy ION, false = DMABUF heaps */
    bool system_heap;       /* Enable system heap */
    bool cma_heap;          /* Enable CMA heap */
    uint64_t cma_size;      /* CMA region size in bytes */
} sv_dma_config_t;

static inline sv_dma_config_t sv_dma_config_default(void)
{
    sv_dma_config_t cfg = {
        .use_ion = false,       /* Prefer DMABUF heaps (modern) */
        .system_heap = true,
        .cma_heap = true,
        .cma_size = 256 * 1024 * 1024,  /* 256MB CMA for GPU/camera */
    };
    return cfg;
}

/*
 * Required kernel config:
 *
 * Modern (preferred):
 *   CONFIG_DMA_SHARED_BUFFER=y
 *   CONFIG_DMABUF_HEAPS=y
 *   CONFIG_DMABUF_HEAPS_SYSTEM=y
 *   CONFIG_DMABUF_HEAPS_CMA=y
 *
 * Legacy (for older Android):
 *   CONFIG_ION=y
 *   CONFIG_ION_SYSTEM_HEAP=y
 *   CONFIG_ION_CMA_HEAP=y
 *
 * CMA reservation (kernel cmdline):
 *   cma=256M@0x440000000
 *
 * Device nodes:
 *   /dev/ion           (legacy)
 *   /dev/dma_heap/system     (modern)
 *   /dev/dma_heap/reserved   (CMA)
 */

#endif /* SILICONV_DMABUF_H */
