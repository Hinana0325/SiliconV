/*
 * Minimal lzfse stub for SiliconV (Apple IMG4 decompression)
 * Provides stub implementations that return failure.
 * Replace with real liblzfse for full Apple kernel support.
 */

#ifndef LZFSE_STUB_H
#define LZFSE_STUB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stub: LZFSE decompression — always fails (no compressed data support) */
static inline size_t lzfse_decode_buffer(uint8_t *__restrict dst_buffer,
                                          size_t dst_size,
                                          const uint8_t *__restrict src_buffer,
                                          size_t src_size,
                                          void *__scratch_buffer)
{
    (void)dst_buffer;
    (void)dst_size;
    (void)src_buffer;
    (void)src_size;
    (void)__scratch_buffer;
    return 0;  /* 0 = failure */
}

/* Stub: LZFSE compression — always fails */
static inline size_t lzfse_encode_buffer(uint8_t *__restrict dst_buffer,
                                          size_t dst_size,
                                          const uint8_t *__restrict src_buffer,
                                          size_t src_size,
                                          void *__scratch_buffer)
{
    (void)dst_buffer;
    (void)dst_size;
    (void)src_buffer;
    (void)src_size;
    (void)__scratch_buffer;
    return 0;
}

/* Stub: scratch buffer size */
static inline size_t lzfse_encode_scratch_size(void) { return 0; }
static inline size_t lzfse_decode_scratch_size(void) { return 0; }

#ifdef __cplusplus
}
#endif

#endif /* LZFSE_STUB_H */
