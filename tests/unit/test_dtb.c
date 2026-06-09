#include "dtb.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static int contains_bytes(const uint8_t *haystack, size_t haystack_len,
                          const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > haystack_len)
        return 0;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0)
            return 1;
    }
    return 0;
}

int main(void)
{
    uint8_t blob[16384];
    memset(blob, 0, sizeof(blob));

    dtb_config_t cfg = dtb_config_default(2, 512ULL * 1024ULL * 1024ULL);
    cfg.num_virtio = 2;
    cfg.cmdline = "console=ttyAMA0 root=/dev/vda rw";

    int size = dtb_generate(blob, sizeof(blob), &cfg);
    CHECK(size > 0);
    CHECK(size <= (int)sizeof(blob));
    CHECK(be32(blob + 0) == 0xd00dfeed);
    CHECK(be32(blob + 4) == (uint32_t)size);
    CHECK(be32(blob + 20) == 17);
    CHECK(be32(blob + 24) == 16);

    CHECK(contains_bytes(blob, (size_t)size, "SiliconV Virtual Machine"));
    CHECK(contains_bytes(blob, (size_t)size, "virtio,mmio"));

    return 0;
}
