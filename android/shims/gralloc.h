/*
 * SiliconV — Gralloc Shim (Graphics Memory HAL)
 *
 * Manages graphics buffer allocation for Android.
 * Uses DMABUF heaps (modern) or ION (legacy).
 *
 * Buffer lifecycle:
 *   1. Android requests buffer (gralloc allocate)
 *   2. Shim allocates from DMABUF heap
 *   3. Returns fd + stride to caller
 *   4. Buffer shared with virtio-gpu via fd
 *   5. On free: close fd, return to heap
 */

#ifndef SILICONV_GRALLOC_H
#define SILICONV_GRALLOC_H

#include <stdint.h>
#include <stdbool.h>

/* ── Pixel Formats (from Android) ──────────────── */
#define HAL_PIXEL_FORMAT_RGBA_8888     1
#define HAL_PIXEL_FORMAT_RGBX_8888     2
#define HAL_PIXEL_FORMAT_RGB_888       3
#define HAL_PIXEL_FORMAT_RGB_565       4
#define HAL_PIXEL_FORMAT_BGRA_8888     5
#define HAL_PIXEL_FORMAT_YCbCr_422_SP  0x10
#define HAL_PIXEL_FORMAT_YCrCb_420_SP  0x11
#define HAL_PIXEL_FORMAT_YCbCr_420_888 0x23
#define HAL_PIXEL_FORMAT_YV12          0x32315659
#define HAL_PIXEL_FORMAT_NV12          0x3231564E
#define HAL_PIXEL_FORMAT_NV21          0x3132564E

/* ── Buffer Usage Flags ────────────────────────── */
#define GRALLOC_USAGE_HW_RENDER        (1 << 4)
#define GRALLOC_USAGE_HW_TEXTURE       (1 << 5)
#define GRALLOC_USAGE_HW_COMPOSER      (1 << 6)
#define GRALLOC_USAGE_HW_FB            (1 << 7)
#define GRALLOC_USAGE_SW_READ_OFTEN    (1 << 12)
#define GRALLOC_USAGE_SW_WRITE_OFTEN   (1 << 13)

/* ── Buffer Descriptor ─────────────────────────── */
typedef struct {
    int      width;
    int      height;
    int      format;        /* HAL_PIXEL_FORMAT_* */
    uint64_t usage;         /* GRALLOC_USAGE_* */
    int      layer_count;   /* For multi-layer (usually 1) */
} sv_gralloc_desc_t;

/* ── Allocated Buffer ──────────────────────────── */
typedef struct {
    int      fd;            /* DMABUF file descriptor */
    uint64_t size;          /* Buffer size in bytes */
    int      stride;        /* Row stride in pixels */
    int      format;
    int      width;
    int      height;
    uint64_t usage;
    void    *mapped_addr;   /* mmap'd address (if mapped) */
} sv_gralloc_buffer_t;

/* ── API ───────────────────────────────────────── */

/* Allocate a graphics buffer */
int sv_gralloc_alloc(const sv_gralloc_desc_t *desc,
                     sv_gralloc_buffer_t *buf);

/* Free a graphics buffer */
int sv_gralloc_free(sv_gralloc_buffer_t *buf);

/* Lock buffer for CPU access */
int sv_gralloc_lock(sv_gralloc_buffer_t *buf, void **addr);

/* Unlock buffer after CPU access */
int sv_gralloc_unlock(sv_gralloc_buffer_t *buf);

/* Get buffer info */
int sv_gralloc_info(const sv_gralloc_buffer_t *buf,
                    sv_gralloc_desc_t *desc);

#endif /* SILICONV_GRALLOC_H */
