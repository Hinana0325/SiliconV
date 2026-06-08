/*
 * SiliconV — Virtio-Block Device
 *
 * Emulates a block device (disk) for the guest.
 * The guest sees a standard virtio-blk device as its root filesystem.
 *
 * Reference: Virtio 1.2 Specification, Section 5.2
 */

#ifndef SILICONV_VIRTIO_BLK_H
#define SILICONV_VIRTIO_BLK_H

#include "../transport/virtio_mmio.h"
#include <stdio.h>

/* ── Virtio-Blk Feature Bits ───────────────────── */
#define VIRTIO_BLK_F_SIZE_MAX    1   /* Max segment size */
#define VIRTIO_BLK_F_SEG_MAX     2   /* Max number of segments */
#define VIRTIO_BLK_F_GEOMETRY    4   /* Disk geometry */
#define VIRTIO_BLK_F_RO          5   /* Read-only */
#define VIRTIO_BLK_F_BLK_SIZE    6   /* Block size */
#define VIRTIO_BLK_F_FLUSH       9   /* Cache flush command */
#define VIRTIO_BLK_F_TOPOLOGY    10  /* Topology info */
#define VIRTIO_BLK_F_CONFIG_WCE  11  /* Writeback config */

/* ── Request Types ─────────────────────────────── */
#define VIRTIO_BLK_T_IN          0   /* Read */
#define VIRTIO_BLK_T_OUT         1   /* Write */
#define VIRTIO_BLK_T_FLUSH       4   /* Flush */
#define VIRTIO_BLK_T_GET_ID      8   /* Get device ID */
#define VIRTIO_BLK_T_DISCARD     11  /* Discard */
#define VIRTIO_BLK_T_WRITE_ZEROES 13 /* Write zeroes */

/* ── Status Codes ──────────────────────────────── */
#define VIRTIO_BLK_S_OK          0
#define VIRTIO_BLK_S_IOERR       1
#define VIRTIO_BLK_S_UNSUPP      2

/* ── Config Space ──────────────────────────────── */
typedef struct {
    uint64_t capacity;         /* Capacity in 512-byte sectors */
    uint32_t size_max;         /* Max segment size */
    uint32_t seg_max;          /* Max segments */
    struct {
        uint16_t cylinders;
        uint8_t  heads;
        uint8_t  sectors;
    } geometry;
    uint32_t blk_size;         /* Block size */
    uint8_t  physical_block_exp;
    uint8_t  alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
} __attribute__((packed)) virtio_blk_config_t;

/* ── Device State ──────────────────────────────── */
typedef struct {
    virtio_device_t vdev;         /* Must be first (polymorphism) */
    virtio_blk_config_t config;
    FILE *disk_image;             /* Backing file */
    bool read_only;
    char device_id[256];          /* Device ID string */
} virtio_blk_t;

/* ── API ───────────────────────────────────────── */

/* Create a virtio-blk device backed by a file */
int virtio_blk_init(virtio_blk_t *blk, const char *image_path,
                    bool read_only, int irq_num,
                    uint8_t *guest_ram, uint64_t ram_base, uint64_t ram_size);

/* Destroy the device */
void virtio_blk_destroy(virtio_blk_t *blk);

/* Get the virtio device handle (for MMIO registration) */
virtio_device_t* virtio_blk_get_vdev(virtio_blk_t *blk);

#endif /* SILICONV_VIRTIO_BLK_H */
