/*
 * SiliconV — Apple NVRAM (Implementation)
 *
 * Simple NVRAM emulation providing key-value storage for
 * boot-args and system parameters.
 */

#include "apple_nvram.h"
#include <string.h>
#include <stdio.h>

void apple_nvram_init(apple_nvram_state_t *nvram)
{
    memset(nvram, 0, sizeof(*nvram));

    /* Initial NVRAM signature */
    nvram->nvram[0] = 'N';
    nvram->nvram[1] = 'V';
    nvram->nvram[2] = 'R';
    nvram->nvram[3] = 'M';

    /* Object pool offset */
    *(uint32_t *)(nvram->nvram + 4) = APPLE_NVRAM_OFFSET_OBJ;
    *(uint32_t *)(nvram->nvram + 8) = 0; /* Number of objects */

    nvram->write_enable = true;
}

void apple_nvram_set(apple_nvram_state_t *nvram,
                     const char *key, const char *value)
{
    if (!nvram->write_enable || !key || !value)
        return;

    uint32_t obj_off = *(uint32_t *)(nvram->nvram + 4);
    uint32_t *num_objs = (uint32_t *)(nvram->nvram + 8);
    uint32_t current = obj_off;

    /* Simple format: type(1) + key_len(1) + val_len(2) + key_data + val_data */
    uint8_t type = 1; /* String object */
    uint8_t key_len = (uint8_t)strlen(key);
    uint16_t val_len = (uint16_t)strlen(value);
    uint32_t entry_size = 1 + 1 + 2 + key_len + val_len;

    if (current + entry_size > APPLE_NVRAM_SIZE)
        return;

    nvram->nvram[current] = type;
    nvram->nvram[current + 1] = key_len;
    *(uint16_t *)(nvram->nvram + current + 2) = val_len;
    memcpy(nvram->nvram + current + 4, key, key_len);
    memcpy(nvram->nvram + current + 4 + key_len, value, val_len);

    (*num_objs)++;
    *(uint32_t *)(nvram->nvram + 4) = current + entry_size;
}

const char* apple_nvram_get(apple_nvram_state_t *nvram, const char *key)
{
    if (!key) return NULL;

    uint32_t obj_off = *(uint32_t *)(nvram->nvram + 4);
    uint32_t num_objs = *(uint32_t *)(nvram->nvram + 8);
    uint32_t current = APPLE_NVRAM_OFFSET_OBJ;

    for (uint32_t i = 0; i < num_objs && current < obj_off; i++) {
        uint8_t type = nvram->nvram[current];
        uint8_t key_len = nvram->nvram[current + 1];
        uint16_t val_len = *(uint16_t *)(nvram->nvram + current + 2);

        if (current + 4 + key_len + val_len > obj_off)
            return NULL;

        if (type == 1 && (size_t)key_len == strlen(key) &&
            memcmp(nvram->nvram + current + 4, key, key_len) == 0) {
            /* Found! Return pointer to value (caller should copy) */
            /* This is a hack for the simple NVRAM — not thread-safe */
            static char val_buf[256];
            uint16_t copy_len = val_len < 255 ? val_len : 255;
            memcpy(val_buf, nvram->nvram + current + 4 + key_len, copy_len);
            val_buf[copy_len] = '\0';
            return val_buf;
        }

        current += 4 + key_len + val_len;
    }

    return NULL;
}

void apple_nvram_set_boot_arg(apple_nvram_state_t *nvram, const char *arg)
{
    /* Store in NVRAM as "boot-args" key */
    /* Also store individual boot-args for iBoot compatibility */
    apple_nvram_set(nvram, "boot-args", arg);

    /* Also set as nvram variable */
    char nvram_key[64];
    snprintf(nvram_key, sizeof(nvram_key), "nvram.%s", "boot-args");
    apple_nvram_set(nvram, nvram_key, arg);
}

/* ── MMIO ───────────────────────────────────────── */
uint64_t apple_nvram_mmio_read(apple_nvram_state_t *nvram, uint64_t offset, int size)
{
    (void)size;
    if (offset >= APPLE_NVRAM_SIZE) return 0;

    uint64_t val = 0;
    memcpy(&val, nvram->nvram + offset, size < 8 ? size : 8);
    return val;
}

void apple_nvram_mmio_write(apple_nvram_state_t *nvram, uint64_t offset,
                             uint64_t value, int size)
{
    (void)size;
    if (offset >= APPLE_NVRAM_SIZE || !nvram->write_enable) return;

    memcpy(nvram->nvram + offset, &value, size < 8 ? size : 8);
}
