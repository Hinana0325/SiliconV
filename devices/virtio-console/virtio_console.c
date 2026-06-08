/*
 * SiliconV — Virtio-Console Device (Implementation)
 *
 * Handles guest ↔ host console I/O.
 * Queue 0: guest → host (TX)
 * Queue 1: host → guest (RX)
 */

#include "virtio_console.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Device Ops ────────────────────────────────── */

static uint64_t console_get_features(virtio_device_t *dev)
{
    (void)dev;
    uint64_t features = 0;
    features |= (1ULL << VIRTIO_CONSOLE_F_SIZE);
    return features;
}

static uint64_t console_config_read(virtio_device_t *dev, uint64_t offset, int size)
{
    virtio_console_t *con = (virtio_console_t*)dev;
    uint8_t *cfg = (uint8_t*)&con->config;

    if (offset + size > sizeof(con->config))
        return 0;

    uint64_t val = 0;
    memcpy(&val, cfg + offset, size);
    return val;
}

static void console_config_write(virtio_device_t *dev, uint64_t offset,
                                  uint64_t value, int size)
{
    (void)dev; (void)offset; (void)value; (void)size;
    /* Config space is read-only */
}

static void console_process_tx(virtio_console_t *con, int queue_idx)
{
    virtio_device_t *vdev = &con->vdev;
    uint16_t head_idx;

    while (virtio_get_avail(vdev, queue_idx, &head_idx) == 0) {
        virtio_desc_t *desc = virtio_get_desc(vdev, queue_idx, head_idx);
        if (!desc) break;

        /* Data from guest — write to host output */
        if (desc->len > 0 && con->host_out) {
            uint8_t *data = vdev->guest_ram + desc->addr - vdev->guest_ram_base;
            fwrite(data, 1, desc->len, con->host_out);
            fflush(con->host_out);
        }

        virtio_put_used(vdev, queue_idx, head_idx, desc->len);
    }

    virtio_raise_interrupt(vdev, VIRTIO_INT_VRING);
}

static void console_queue_notify(virtio_device_t *dev, int queue_idx)
{
    virtio_console_t *con = (virtio_console_t*)dev;

    if (queue_idx == 0) {
        /* TX: guest → host */
        console_process_tx(con, 0);
    }
    /* RX (queue 1) is host-initiated, not triggered by guest */
}

static void console_reset(virtio_device_t *dev)
{
    (void)dev;
}

static void console_set_status(virtio_device_t *dev, uint32_t status)
{
    (void)dev; (void)status;
}

static const virtio_dev_ops_t console_ops = {
    .name = "virtio-console",
    .device_id = 3,          /* Virtio console device */
    .vendor_id = 0x554D4551, /* QEMU vendor */
    .get_features = console_get_features,
    .config_read = console_config_read,
    .config_write = console_config_write,
    .queue_notify = console_queue_notify,
    .reset = console_reset,
    .set_status = console_set_status,
};

/* ── Public API ────────────────────────────────── */

int virtio_console_init(virtio_console_t *con, int irq_num,
                         uint8_t *guest_ram, uint64_t ram_base,
                         uint64_t ram_size)
{
    memset(con, 0, sizeof(*con));

    con->host_in = stdin;
    con->host_out = stdout;

    con->config.cols = 80;
    con->config.rows = 25;
    con->config.max_nr_ports = 1;
    con->config.emerg_wr = 0;

    snprintf(con->device_id, sizeof(con->device_id), "siliconv-console-0");

    /* 2 queues: 0=TX (guest→host), 1=RX (host→guest) */
    virtio_init(&con->vdev, &console_ops, 2, irq_num,
                guest_ram, ram_base, ram_size);
    con->vdev.opaque = con;

    printf("virtio-console: initialized (80x25)\n");
    return 0;
}

void virtio_console_destroy(virtio_console_t *con)
{
    (void)con;
    /* Nothing to clean up */
}

void virtio_console_set_streams(virtio_console_t *con,
                                 FILE *host_in, FILE *host_out)
{
    con->host_in = host_in;
    con->host_out = host_out;
}

virtio_device_t* virtio_console_get_vdev(virtio_console_t *con)
{
    return &con->vdev;
}
