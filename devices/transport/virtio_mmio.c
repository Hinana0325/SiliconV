/*
 * SiliconV — Virtio MMIO Transport (Implementation)
 *
 * Handles the generic virtio-mmio protocol.
 * Dispatches to device-specific ops when needed.
 */

#include "virtio_mmio.h"
#include <string.h>
#include <stdio.h>

/* ── Initialization ────────────────────────────── */

void virtio_init(virtio_device_t *dev, const virtio_dev_ops_t *ops,
                 int num_queues, int irq_num,
                 uint8_t *guest_ram, uint64_t ram_base, uint64_t ram_size)
{
    memset(dev, 0, sizeof(*dev));
    dev->ops = ops;
    dev->num_queues = num_queues;
    dev->irq_num = irq_num;
    dev->guest_ram = guest_ram;
    dev->guest_ram_base = ram_base;
    dev->guest_ram_size = ram_size;
}

/* ── Interrupt Helpers ─────────────────────────── */

void virtio_raise_interrupt(virtio_device_t *dev, uint32_t mask)
{
    dev->int_status |= mask;
    /* TODO: notify GIC to raise IRQ dev->irq_num */
}

/* ── Descriptor Helpers ────────────────────────── */

virtio_desc_t* virtio_get_desc(virtio_device_t *dev, int queue_idx, int idx)
{
    if (queue_idx >= dev->num_queues) return NULL;
    virtio_queue_t *q = &dev->queues[queue_idx];
    if (idx >= q->size) return NULL;
    if (!q->desc) return NULL;
    return &q->desc[idx];
}

int virtio_get_avail(virtio_device_t *dev, int queue_idx, uint16_t *idx_out)
{
    if (queue_idx >= dev->num_queues) return -1;
    virtio_queue_t *q = &dev->queues[queue_idx];

    if (!q->ready || !q->avail_ring || !q->avail_idx)
        return -1;

    /* Check if there are new available buffers */
    uint16_t avail_idx = *q->avail_idx;
    if (q->last_avail_idx == avail_idx)
        return -1;  /* No new buffers */

    /* Get descriptor index from available ring */
    uint16_t ring_idx = q->last_avail_idx % q->size;
    *idx_out = q->avail_ring[ring_idx];
    q->last_avail_idx++;

    return 0;
}

void virtio_put_used(virtio_device_t *dev, int queue_idx,
                     uint16_t desc_idx, uint32_t len)
{
    if (queue_idx >= dev->num_queues) return;
    virtio_queue_t *q = &dev->queues[queue_idx];

    if (!q->used_idx) return;

    uint16_t used_idx = *q->used_idx;
    uint16_t ring_idx = used_idx % q->size;

    /* Write to used ring: struct { le32 id; le32 len; } */
    struct { uint32_t id; uint32_t len; } __attribute__((packed)) *used_elem;
    used_elem = (void*)(q->used_addr - dev->guest_ram_base + dev->guest_ram + 4 + ring_idx * 8);
    used_elem->id = desc_idx;
    used_elem->len = len;

    /* Update used index */
    *q->used_idx = used_idx + 1;
}

/* ── MMIO Read ─────────────────────────────────── */

uint64_t virtio_mmio_read(virtio_device_t *dev, uint64_t offset, int size)
{
    switch (offset) {
    case VIRTIO_MMIO_MAGIC:
        return 0x74726976;  /* "virt" */

    case VIRTIO_MMIO_VERSION:
        return 2;  /* Modern */

    case VIRTIO_MMIO_DEVICE_ID:
        return dev->ops->device_id;

    case VIRTIO_MMIO_VENDOR_ID:
        return dev->ops->vendor_id ? dev->ops->vendor_id : 0x554D4551;

    case VIRTIO_MMIO_DEVICE_FEATURES: {
        uint64_t features = 0;
        if (dev->ops->get_features)
            features = dev->ops->get_features(dev);
        if (dev->device_features_sel == 0)
            return (uint32_t)features;
        else
            return (uint32_t)(features >> 32);
    }

    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        return VIRTQUEUE_MAX_SIZE;

    case VIRTIO_MMIO_QUEUE_READY:
        if (dev->queue_sel < dev->num_queues)
            return dev->queues[dev->queue_sel].ready ? 1 : 0;
        return 0;

    case VIRTIO_MMIO_INTERRUPT_STATUS:
        return dev->int_status;

    case VIRTIO_MMIO_STATUS:
        return dev->status;

    default:
        break;
    }

    /* Config space read (device-specific) */
    if (offset >= VIRTIO_MMIO_CONFIG && dev->ops->config_read) {
        return dev->ops->config_read(dev, offset - VIRTIO_MMIO_CONFIG, size);
    }

    fprintf(stderr, "virtio: unhandled read offset=0x%lx\n", offset);
    return 0;
}

/* ── MMIO Write ────────────────────────────────── */

void virtio_mmio_write(virtio_device_t *dev, uint64_t offset,
                       uint64_t value, int size)
{
    (void)size;

    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        dev->device_features_sel = value & 1;
        return;

    case VIRTIO_MMIO_DRIVER_FEATURES: {
        if (dev->driver_features_sel == 0)
            dev->driver_features = (dev->driver_features & 0xFFFFFFFF00000000ULL) | (uint32_t)value;
        else
            dev->driver_features = (dev->driver_features & 0x00000000FFFFFFFFULL) | ((uint64_t)(uint32_t)value << 32);
        return;
    }

    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        dev->driver_features_sel = value & 1;
        return;

    case VIRTIO_MMIO_QUEUE_SEL:
        dev->queue_sel = value;
        return;

    case VIRTIO_MMIO_QUEUE_NUM:
        if (dev->queue_sel < dev->num_queues)
            dev->queues[dev->queue_sel].size = value;
        return;

    case VIRTIO_MMIO_QUEUE_READY:
        if (dev->queue_sel < dev->num_queues)
            dev->queues[dev->queue_sel].ready = (value & 1);
        return;

    case VIRTIO_MMIO_QUEUE_NOTIFY:
        if (value < (uint32_t)dev->num_queues && dev->ops->queue_notify) {
            dev->ops->queue_notify(dev, value);
        }
        return;

    case VIRTIO_MMIO_INTERRUPT_ACK:
        dev->int_status &= ~value;
        return;

    case VIRTIO_MMIO_STATUS: {
        uint32_t old = dev->status;
        dev->status = value;

        /* Status transitions */
        if ((value & VIRTIO_STATUS_DRIVER_OK) && !(old & VIRTIO_STATUS_DRIVER_OK)) {
            if (dev->ops->set_status)
                dev->ops->set_status(dev, value);
        }

        /* Reset */
        if (value == 0 && old != 0) {
            if (dev->ops->reset)
                dev->ops->reset(dev);
            dev->driver_features = 0;
            dev->device_features_sel = 0;
            dev->driver_features_sel = 0;
            dev->int_status = 0;
            memset(dev->queues, 0, sizeof(dev->queues));
        }
        return;
    }

    /* Queue descriptor table address */
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
    case VIRTIO_MMIO_QUEUE_DESC_HIGH: {
        virtio_queue_t *q = &dev->queues[dev->queue_sel];
        if (offset == VIRTIO_MMIO_QUEUE_DESC_LOW)
            q->desc_addr = (q->desc_addr & 0xFFFFFFFF00000000ULL) | (uint32_t)value;
        else
            q->desc_addr = (q->desc_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)(uint32_t)value << 32);

        /* Cache descriptor table pointer */
        if (q->desc_addr >= dev->guest_ram_base &&
            q->desc_addr < dev->guest_ram_base + dev->guest_ram_size) {
            q->desc = (virtio_desc_t*)(dev->guest_ram + (q->desc_addr - dev->guest_ram_base));
        }
        return;
    }

    /* Available ring address */
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH: {
        virtio_queue_t *q = &dev->queues[dev->queue_sel];
        if (offset == VIRTIO_MMIO_QUEUE_AVAIL_LOW)
            q->avail_addr = (q->avail_addr & 0xFFFFFFFF00000000ULL) | (uint32_t)value;
        else
            q->avail_addr = (q->avail_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)(uint32_t)value << 32);

        /* Cache: avail_ring = avail_addr + 2 (skip flags), avail_idx = avail_addr + 0 */
        if (q->avail_addr >= dev->guest_ram_base &&
            q->avail_addr < dev->guest_ram_base + dev->guest_ram_size) {
            uint8_t *base = dev->guest_ram + (q->avail_addr - dev->guest_ram_base);
            q->avail_idx = (uint16_t*)base;
            q->avail_ring = (uint16_t*)(base + 2);
        }
        return;
    }

    /* Used ring address */
    case VIRTIO_MMIO_QUEUE_USED_LOW:
    case VIRTIO_MMIO_QUEUE_USED_HIGH: {
        virtio_queue_t *q = &dev->queues[dev->queue_sel];
        if (offset == VIRTIO_MMIO_QUEUE_USED_LOW)
            q->used_addr = (q->used_addr & 0xFFFFFFFF00000000ULL) | (uint32_t)value;
        else
            q->used_addr = (q->used_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)(uint32_t)value << 32);

        /* Cache: used_idx = used_addr + 2 (skip flags) */
        if (q->used_addr >= dev->guest_ram_base &&
            q->used_addr < dev->guest_ram_base + dev->guest_ram_size) {
            uint8_t *base = dev->guest_ram + (q->used_addr - dev->guest_ram_base);
            q->used_idx = (uint16_t*)(base + 2);
        }
        return;
    }

    default:
        break;
    }

    /* Config space write (device-specific) */
    if (offset >= VIRTIO_MMIO_CONFIG && dev->ops->config_write) {
        dev->ops->config_write(dev, offset - VIRTIO_MMIO_CONFIG, value, size);
        return;
    }

    fprintf(stderr, "virtio: unhandled write offset=0x%lx val=0x%lx\n", offset, value);
}
