/*
 * SiliconV — Virtio MMIO Transport
 *
 * Implements the virtio-mmio transport layer.
 * Each virtio device sits behind an MMIO region.
 *
 * Reference: Virtio 1.2 Specification, Section 4.2.4
 */

#ifndef SILICONV_VIRTIO_MMIO_H
#define SILICONV_VIRTIO_MMIO_H

#include <stdint.h>
#include <stdbool.h>

/* ── MMIO Register Offsets ─────────────────────── */
#define VIRTIO_MMIO_MAGIC         0x000  /* R: Magic value (0x74726976) */
#define VIRTIO_MMIO_VERSION       0x004  /* R: Version (2 = modern) */
#define VIRTIO_MMIO_DEVICE_ID     0x008  /* R: Device type */
#define VIRTIO_MMIO_VENDOR_ID     0x00C  /* R: Virtio vendor ID (0x554D4551) */
#define VIRTIO_MMIO_DEVICE_FEATURES    0x010  /* R: Features offered */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014  /* W: Feature select (high/low) */
#define VIRTIO_MMIO_DRIVER_FEATURES    0x020  /* W: Features accepted */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024  /* W: Feature select (high/low) */
#define VIRTIO_MMIO_QUEUE_SEL      0x030  /* W: Queue select */
#define VIRTIO_MMIO_QUEUE_NUM_MAX  0x034  /* R: Queue max size */
#define VIRTIO_MMIO_QUEUE_NUM      0x038  /* W: Queue size */
#define VIRTIO_MMIO_QUEUE_READY    0x044  /* RW: Queue ready */
#define VIRTIO_MMIO_QUEUE_NOTIFY   0x050  /* W: Queue notify */
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060  /* R: Interrupt status */
#define VIRTIO_MMIO_INTERRUPT_ACK  0x064  /* W: Interrupt acknowledge */
#define VIRTIO_MMIO_STATUS         0x070  /* RW: Device status */
#define VIRTIO_MMIO_QUEUE_DESC_LOW   0x080  /* W: Descriptor area (low 32) */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH  0x084  /* W: Descriptor area (high 32) */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW  0x090  /* W: Available area (low 32) */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094  /* W: Available area (high 32) */
#define VIRTIO_MMIO_QUEUE_USED_LOW   0x0A0  /* W: Used area (low 32) */
#define VIRTIO_MMIO_QUEUE_USED_HIGH  0x0A4  /* W: Used area (high 32) */
#define VIRTIO_MMIO_SHM_SEL          0x0AC  /* W: Shared memory select */
#define VIRTIO_MMIO_SHM_LEN_LOW      0x0B0  /* R: Shared memory length (low) */
#define VIRTIO_MMIO_SHM_LEN_HIGH     0x0B4  /* R: Shared memory length (high) */
#define VIRTIO_MMIO_SHM_BASE_LOW     0x0B8  /* R: Shared memory base (low) */
#define VIRTIO_MMIO_SHM_BASE_HIGH    0x0BC  /* R: Shared memory base (high) */
#define VIRTIO_MMIO_QUEUE_RESET      0x0C0  /* W: Queue reset */
#define VIRTIO_MMIO_CONFIG           0x100  /* RW: Config space start */

/* ── Status Bits ───────────────────────────────── */
#define VIRTIO_STATUS_ACKNOWLEDGE  (1 << 0)
#define VIRTIO_STATUS_DRIVER       (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK    (1 << 2)
#define VIRTIO_STATUS_FEATURES_OK  (1 << 3)
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET (1 << 6)
#define VIRTIO_STATUS_FAILED       (1 << 7)

/* ── Interrupt Status Bits ─────────────────────── */
#define VIRTIO_INT_VRING           (1 << 0)  /* Used ring update */
#define VIRTIO_INT_CONFIG          (1 << 1)  /* Config change */

/* ── Virtqueue Descriptor ──────────────────────── */
typedef struct {
    uint64_t addr;   /* Guest physical address */
    uint32_t len;    /* Length */
    uint16_t flags;  /* VRING_DESC_F_NEXT, VRING_DESC_F_WRITE */
    uint16_t next;   /* Next descriptor index */
} virtio_desc_t;

#define VRING_DESC_F_NEXT   1
#define VRING_DESC_F_WRITE  2
#define VRING_DESC_F_INDIRECT 4

/* ── Virtqueue ─────────────────────────────────── */
#define VIRTQUEUE_MAX_SIZE 256

typedef struct {
    uint16_t size;       /* Queue size (num descriptors) */
    bool     ready;      /* Queue ready flag */

    /* Guest physical addresses */
    uint64_t desc_addr;  /* Descriptor table */
    uint64_t avail_addr; /* Available ring */
    uint64_t used_addr;  /* Used ring */

    /* Cached pointers (set when queue is configured) */
    virtio_desc_t *desc;
    uint16_t *avail_ring;
    uint16_t *avail_idx;
    uint16_t *used_idx;

    /* Tracking */
    uint16_t last_avail_idx;  /* Last consumed available index */
} virtio_queue_t;

/* ── Device Ops ────────────────────────────────── */
typedef struct virtio_device virtio_device_t;

typedef struct {
    const char *name;
    uint32_t device_id;
    uint32_t vendor_id;

    /* Device-specific feature bits offered */
    uint64_t (*get_features)(virtio_device_t *dev);

    /* Device-specific config read (offset from 0x100) */
    uint64_t (*config_read)(virtio_device_t *dev, uint64_t offset, int size);

    /* Device-specific config write */
    void (*config_write)(virtio_device_t *dev, uint64_t offset,
                         uint64_t value, int size);

    /* Queue notification — device should process available buffers */
    void (*queue_notify)(virtio_device_t *dev, int queue_idx);

    /* Device reset */
    void (*reset)(virtio_device_t *dev);

    /* Device status change */
    void (*set_status)(virtio_device_t *dev, uint32_t status);
} virtio_dev_ops_t;

/* ── Virtio Device ─────────────────────────────── */
struct virtio_device {
    const virtio_dev_ops_t *ops;
    void *opaque;  /* Device-specific data */

    /* Transport state */
    uint32_t status;
    uint32_t device_features_sel;  /* 0 = low, 1 = high */
    uint32_t driver_features_sel;
    uint64_t device_features;      /* Negotiated features */
    uint64_t driver_features;
    int      queue_sel;            /* Currently selected queue */
    uint32_t int_status;           /* Interrupt status */

    /* Virtqueues */
    virtio_queue_t queues[16];
    int num_queues;

    /* IRQ */
    int irq_num;

    /* Guest memory base for DMA */
    uint8_t *guest_ram;
    uint64_t guest_ram_base;
    uint64_t guest_ram_size;
};

/* ── API ───────────────────────────────────────── */

/* Initialize a virtio device */
void virtio_init(virtio_device_t *dev, const virtio_dev_ops_t *ops,
                 int num_queues, int irq_num,
                 uint8_t *guest_ram, uint64_t ram_base, uint64_t ram_size);

/* Raise interrupt on the device */
void virtio_raise_interrupt(virtio_device_t *dev, uint32_t mask);

/* MMIO read/write handlers (dispatched to transport) */
uint64_t virtio_mmio_read(virtio_device_t *dev, uint64_t offset, int size);
void virtio_mmio_write(virtio_device_t *dev, uint64_t offset,
                       uint64_t value, int size);

/* Helper: get descriptor from queue */
virtio_desc_t* virtio_get_desc(virtio_device_t *dev, int queue_idx, int idx);

/* Helper: process available ring */
int virtio_get_avail(virtio_device_t *dev, int queue_idx, uint16_t *idx_out);

/* Helper: put used buffer */
void virtio_put_used(virtio_device_t *dev, int queue_idx,
                     uint16_t desc_idx, uint32_t len);

#endif /* SILICONV_VIRTIO_MMIO_H */
