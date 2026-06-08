/*
 * SiliconV — Virtio-Block Device (Implementation)
 *
 * Handles block read/write requests from the guest.
 * Backing store is a raw image file on the host.
 */

#include "virtio_blk.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Request Header (guest → host) ─────────────── */
typedef struct {
    uint32_t type;      /* VIRTIO_BLK_T_* */
    uint32_t reserved;
    uint64_t sector;    /* Sector number */
} __attribute__((packed)) virtio_blk_req_header_t;

/* ── Device Ops ────────────────────────────────── */

static uint64_t blk_get_features(virtio_device_t *dev)
{
    virtio_blk_t *blk = (virtio_blk_t*)dev;
    uint64_t features = 0;

    features |= (1ULL << VIRTIO_BLK_F_SIZE_MAX);
    features |= (1ULL << VIRTIO_BLK_F_SEG_MAX);
    features |= (1ULL << VIRTIO_BLK_F_GEOMETRY);
    features |= (1ULL << VIRTIO_BLK_F_BLK_SIZE);
    features |= (1ULL << VIRTIO_BLK_F_TOPOLOGY);
    features |= (1ULL << VIRTIO_BLK_F_FLUSH);

    if (blk->read_only)
        features |= (1ULL << VIRTIO_BLK_F_RO);

    return features;
}

static uint64_t blk_config_read(virtio_device_t *dev, uint64_t offset, int size)
{
    virtio_blk_t *blk = (virtio_blk_t*)dev;
    uint8_t *cfg = (uint8_t*)&blk->config;

    if (offset + size > sizeof(blk->config))
        return 0;

    uint64_t val = 0;
    memcpy(&val, cfg + offset, size);
    return val;
}

static void blk_config_write(virtio_device_t *dev, uint64_t offset,
                             uint64_t value, int size)
{
    (void)dev; (void)offset; (void)value; (void)size;
    /* Config space is read-only for block device */
}

static void blk_process_request(virtio_blk_t *blk, int queue_idx)
{
    virtio_device_t *vdev = &blk->vdev;
    uint16_t head_idx;

    while (virtio_get_avail(vdev, queue_idx, &head_idx) == 0) {
        /* Walk the descriptor chain */
        virtio_desc_t *desc = virtio_get_desc(vdev, queue_idx, head_idx);
        if (!desc) break;

        /* First descriptor: request header (read-only from guest) */
        virtio_blk_req_header_t *hdr = (virtio_blk_req_header_t*)
            (vdev->guest_ram + desc->addr - vdev->guest_ram_base);
        uint32_t hdr_len = desc->len;

        /* Second descriptor: data buffer */
        virtio_desc_t *data_desc = NULL;
        if (desc->flags & VRING_DESC_F_NEXT) {
            data_desc = virtio_get_desc(vdev, queue_idx, desc->next);
        }

        /* Third descriptor: status byte (write to guest) */
        virtio_desc_t *status_desc = NULL;
        if (data_desc && (data_desc->flags & VRING_DESC_F_NEXT)) {
            status_desc = virtio_get_desc(vdev, queue_idx, data_desc->next);
        }

        uint8_t status = VIRTIO_BLK_S_OK;
        uint32_t data_len = 0;

        switch (hdr->type) {
        case VIRTIO_BLK_T_IN: {
            /* Read from disk into guest buffer */
            if (!data_desc || !blk->disk_image) {
                status = VIRTIO_BLK_S_IOERR;
                break;
            }

            uint64_t offset = hdr->sector * 512;
            data_len = data_desc->len;

            fseek(blk->disk_image, offset, SEEK_SET);
            size_t nread = fread(vdev->guest_ram + data_desc->addr - vdev->guest_ram_base,
                                 1, data_len, blk->disk_image);
            if (nread != data_len) {
                status = VIRTIO_BLK_S_IOERR;
                data_len = nread;
            }
            break;
        }

        case VIRTIO_BLK_T_OUT: {
            /* Write from guest buffer to disk */
            if (!data_desc || !blk->disk_image) {
                status = VIRTIO_BLK_S_IOERR;
                break;
            }

            if (blk->read_only) {
                status = VIRTIO_BLK_S_IOERR;
                break;
            }

            uint64_t offset = hdr->sector * 512;
            data_len = data_desc->len;

            fseek(blk->disk_image, offset, SEEK_SET);
            size_t nwritten = fwrite(vdev->guest_ram + data_desc->addr - vdev->guest_ram_base,
                                     1, data_len, blk->disk_image);
            if (nwritten != data_len) {
                status = VIRTIO_BLK_S_IOERR;
            }
            break;
        }

        case VIRTIO_BLK_T_FLUSH: {
            if (blk->disk_image)
                fflush(blk->disk_image);
            break;
        }

        default:
            status = VIRTIO_BLK_S_UNSUPP;
            break;
        }

        /* Write status byte to guest */
        if (status_desc) {
            uint8_t *status_ptr = vdev->guest_ram +
                status_desc->addr - vdev->guest_ram_base;
            *status_ptr = status;
        }

        /* Put buffer in used ring */
        virtio_put_used(vdev, queue_idx, head_idx, hdr_len + data_len + 1);
    }

    /* Notify guest: used ring updated */
    virtio_raise_interrupt(vdev, VIRTIO_INT_VRING);
}

static void blk_queue_notify(virtio_device_t *dev, int queue_idx)
{
    virtio_blk_t *blk = (virtio_blk_t*)dev;
    blk_process_request(blk, queue_idx);
}

static void blk_reset(virtio_device_t *dev)
{
    (void)dev;
    /* Nothing to reset for now */
}

static void blk_set_status(virtio_device_t *dev, uint32_t status)
{
    (void)dev; (void)status;
}

static const virtio_dev_ops_t blk_ops = {
    .name = "virtio-blk",
    .device_id = 2,          /* Virtio block device */
    .vendor_id = 0x554D4551, /* QEMU vendor */
    .get_features = blk_get_features,
    .config_read = blk_config_read,
    .config_write = blk_config_write,
    .queue_notify = blk_queue_notify,
    .reset = blk_reset,
    .set_status = blk_set_status,
};

/* ── Public API ────────────────────────────────── */

int virtio_blk_init(virtio_blk_t *blk, const char *image_path,
                    bool read_only, int irq_num,
                    uint8_t *guest_ram, uint64_t ram_base, uint64_t ram_size)
{
    memset(blk, 0, sizeof(*blk));

    /* Open disk image */
    const char *mode = read_only ? "rb" : "r+b";
    blk->disk_image = fopen(image_path, mode);
    if (!blk->disk_image) {
        fprintf(stderr, "virtio-blk: cannot open %s\n", image_path);
        return -1;
    }

    /* Get image size */
    fseek(blk->disk_image, 0, SEEK_END);
    long size = ftell(blk->disk_image);
    fseek(blk->disk_image, 0, SEEK_SET);

    blk->read_only = read_only;

    /* Configure device */
    blk->config.capacity = size / 512;  /* Sectors */
    blk->config.size_max = 65536;
    blk->config.seg_max = 128;
    blk->config.geometry.cylinders = 0;
    blk->config.geometry.heads = 0;
    blk->config.geometry.sectors = 0;
    blk->config.blk_size = 512;

    /* Set device ID */
    snprintf(blk->device_id, sizeof(blk->device_id), "siliconv-blk-0");

    /* Initialize virtio device */
    virtio_init(&blk->vdev, &blk_ops, 1 /* 1 queue */, irq_num,
                guest_ram, ram_base, ram_size);
    blk->vdev.opaque = blk;

    printf("virtio-blk: %s (%ld sectors, %ld MB)\n",
           image_path, (long)blk->config.capacity,
           (long)(size / (1024 * 1024)));

    return 0;
}

void virtio_blk_destroy(virtio_blk_t *blk)
{
    if (blk->disk_image) {
        fclose(blk->disk_image);
        blk->disk_image = NULL;
    }
}

virtio_device_t* virtio_blk_get_vdev(virtio_blk_t *blk)
{
    return &blk->vdev;
}
