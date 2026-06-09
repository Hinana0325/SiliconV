#include "virtio_mmio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static uint64_t no_features(virtio_device_t *dev)
{
    (void)dev;
    return 0;
}

static const virtio_dev_ops_t test_ops = {
    .name = "test",
    .device_id = 0x42,
    .vendor_id = 0x53565453,
    .get_features = no_features,
};

int main(void)
{
    uint8_t ram[4096];
    memset(ram, 0, sizeof(ram));

    virtio_device_t dev;
    virtio_init(&dev, &test_ops, 1, 40, ram, 0x400000000ULL, sizeof(ram));

    CHECK(virtio_mmio_read(&dev, VIRTIO_MMIO_MAGIC, 4) == 0x74726976);
    CHECK(virtio_mmio_read(&dev, VIRTIO_MMIO_VERSION, 4) == 2);
    CHECK(virtio_mmio_read(&dev, VIRTIO_MMIO_DEVICE_ID, 4) == 0x42);
    CHECK(virtio_mmio_read(&dev, VIRTIO_MMIO_VENDOR_ID, 4) == 0x53565453);

    virtio_mmio_write(&dev, VIRTIO_MMIO_QUEUE_SEL, 0, 4);
    virtio_mmio_write(&dev, VIRTIO_MMIO_QUEUE_NUM, 8, 4);

    uint64_t avail_addr = 0x400000000ULL + 0x100;
    virtio_mmio_write(&dev, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr, 4);
    virtio_mmio_write(&dev, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32), 4);

    uint16_t *avail_flags = (uint16_t *)(ram + 0x100);
    uint16_t *avail_idx = (uint16_t *)(ram + 0x102);
    uint16_t *avail_ring = (uint16_t *)(ram + 0x104);
    *avail_flags = 0;
    *avail_idx = 1;
    avail_ring[0] = 3;

    dev.queues[0].ready = true;
    uint16_t desc_idx = 0;
    CHECK(virtio_get_avail(&dev, 0, &desc_idx) == 0);
    CHECK(desc_idx == 3);
    CHECK(dev.queues[0].last_avail_idx == 1);

    virtio_raise_interrupt(&dev, VIRTIO_INT_VRING);
    CHECK(virtio_mmio_read(&dev, VIRTIO_MMIO_INTERRUPT_STATUS, 4) == VIRTIO_INT_VRING);
    virtio_mmio_write(&dev, VIRTIO_MMIO_INTERRUPT_ACK, VIRTIO_INT_VRING, 4);
    CHECK(virtio_mmio_read(&dev, VIRTIO_MMIO_INTERRUPT_STATUS, 4) == 0);

    return 0;
}
