/*
 * SiliconV — Apple NVRAM
 *
 * Emulates the Apple NVRAM controller used in Apple Silicon SoCs.
 * Provides boot-arg and system parameter storage for XNU.
 */

#ifndef SILICONV_APPLE_NVRAM_H
#define SILICONV_APPLE_NVRAM_H

#include <stdint.h>
#include <stdbool.h>

#define APPLE_NVRAM_SIZE       0x4000   /* 16 KB */
#define APPLE_NVRAM_OFFSET_OBJ 0x0008   /* Object pool offset */

/* ── State ─────────────────────────────────────── */
typedef struct {
    uint8_t  nvram[APPLE_NVRAM_SIZE];
    uint32_t ctrl;
    bool     write_enable;
} apple_nvram_state_t;

/* ── API ───────────────────────────────────────── */
void apple_nvram_init(apple_nvram_state_t *nvram);

/* Set a key-value pair in NVRAM */
void apple_nvram_set(apple_nvram_state_t *nvram,
                     const char *key, const char *value);

/* Get a value by key from NVRAM */
const char* apple_nvram_get(apple_nvram_state_t *nvram, const char *key);

/* MMIO handlers */
uint64_t apple_nvram_mmio_read(apple_nvram_state_t *nvram, uint64_t offset, int size);
void     apple_nvram_mmio_write(apple_nvram_state_t *nvram, uint64_t offset,
                                 uint64_t value, int size);

/* Set a boot-arg in NVRAM */
void apple_nvram_set_boot_arg(apple_nvram_state_t *nvram, const char *arg);

#endif /* SILICONV_APPLE_NVRAM_H */
