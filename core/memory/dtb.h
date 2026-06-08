/*
 * SiliconV — DTB Generator
 *
 * Generates a Flattened Device Tree (FDT) blob at runtime.
 * The DTB matches the SiliconV v0 spec:
 *   - spec/memory/mmio-map.md
 *   - spec/irq/irq-map.md
 *   - spec/boot/dtb-schema.md
 *
 * No external libfdt dependency — we write the binary format directly.
 */

#ifndef SILICONV_DTB_H
#define SILICONV_DTB_H

#include <stdint.h>
#include <stddef.h>

/* DTB generation parameters */
typedef struct {
    int      num_cpus;
    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t uart_base;
    int      uart_irq;
    /* Virtio device list */
    struct {
        uint64_t base;
        int      irq;
        int      device_id;  /* virtio device type */
    } virtio[8];
    int      num_virtio;
    const char *cmdline;
} dtb_config_t;

/* Generate a DTB blob into the provided buffer.
 * Returns the size written, or -1 on error.
 * Buffer should be at least 16KB.
 */
int dtb_generate(uint8_t *buf, size_t bufsize, const dtb_config_t *config);

/* Default SiliconV v0 DTB config */
dtb_config_t dtb_config_default(int num_cpus, uint64_t ram_size);

#endif /* SILICONV_DTB_H */
