/*
 * SiliconV — Virtio-Console Device
 *
 * Emulates a virtio console for guest ↔ host communication.
 * 2 queues: rx (guest reads) and tx (guest writes).
 *
 * Reference: Virtio 1.2 Specification, Section 5.3
 */

#ifndef SILICONV_VIRTIO_CONSOLE_H
#define SILICONV_VIRTIO_CONSOLE_H

#include "../transport/virtio_mmio.h"
#include <stdio.h>

/* ── Virtio-Console Feature Bits ───────────────── */
#define VIRTIO_CONSOLE_F_SIZE      0   /* Console size available */
#define VIRTIO_CONSOLE_F_MULTIPORT 1   /* Multiple ports */
#define VIRTIO_CONSOLE_F_EMERG_WRITE 2 /* Emergency write */

/* ── Config Space ──────────────────────────────── */
typedef struct {
    uint16_t cols;       /* Number of columns */
    uint16_t rows;       /* Number of rows */
    uint32_t max_nr_ports; /* Max ports (if MULTIPORT) */
    uint32_t emerg_wr;   /* Emergency write index */
} __attribute__((packed)) virtio_console_config_t;

/* ── Device State ──────────────────────────────── */
typedef struct {
    virtio_device_t vdev;         /* Must be first (polymorphism) */
    virtio_console_config_t config;
    FILE *host_in;                /* Host input (stdin or pipe) */
    FILE *host_out;               /* Host output (stdout or pipe) */
    char device_id[256];
} virtio_console_t;

/* ── API ───────────────────────────────────────── */

/* Create a virtio-console device */
int virtio_console_init(virtio_console_t *con,
                        int irq_num,
                        uint8_t *guest_ram, uint64_t ram_base,
                        uint64_t ram_size);

/* Destroy the device */
void virtio_console_destroy(virtio_console_t *con);

/* Set host I/O streams (default: stdin/stdout) */
void virtio_console_set_streams(virtio_console_t *con,
                                 FILE *host_in, FILE *host_out);

/* Get the virtio device handle */
virtio_device_t* virtio_console_get_vdev(virtio_console_t *con);

#endif /* SILICONV_VIRTIO_CONSOLE_H */
