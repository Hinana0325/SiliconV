/*
 * SiliconV — Virtio-Net Device (Implementation)
 *
 * Handles network packet TX/RX between guest and host.
 * Queue 0: receiveq (host → guest)
 * Queue 1: transmitq (guest → host)
 *
 * Host backend can be TAP, user-mode, or a stub (loopback/drop).
 */

#include "virtio_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ── Device Ops ────────────────────────────────── */

static uint64_t net_get_features(virtio_device_t *dev)
{
    virtio_net_t *net = (virtio_net_t*)dev;
    uint64_t features = 0;

    features |= (1ULL << VIRTIO_NET_F_MAC);
    features |= (1ULL << VIRTIO_NET_F_STATUS);
    features |= (1ULL << VIRTIO_NET_F_MTU);

    (void)net;
    return features;
}

static uint64_t net_config_read(virtio_device_t *dev, uint64_t offset, int size)
{
    virtio_net_t *net = (virtio_net_t*)dev;
    uint8_t *cfg = (uint8_t*)&net->config;

    if (offset + size > sizeof(net->config))
        return 0;

    uint64_t val = 0;
    memcpy(&val, cfg + offset, size);
    return val;
}

static void net_config_write(virtio_device_t *dev, uint64_t offset,
                              uint64_t value, int size)
{
    (void)dev; (void)offset; (void)value; (void)size;
    /* Config space is read-only for net device */
}

/* ── TX: Guest → Host ──────────────────────────── */

static void net_process_tx(virtio_net_t *net, int queue_idx)
{
    virtio_device_t *vdev = &net->vdev;
    uint16_t head_idx;

    while (virtio_get_avail(vdev, queue_idx, &head_idx) == 0) {
        virtio_desc_t *desc = virtio_get_desc(vdev, queue_idx, head_idx);
        if (!desc) break;

        /* First descriptor: virtio-net header (10 or 12 bytes) */
        uint32_t total_len = 0;
        virtio_desc_t *cur = desc;
        uint8_t pkt_buf[65536];
        uint32_t pkt_len = 0;
        bool first = true;

        /* Walk descriptor chain, collecting packet data */
        while (cur) {
            uint8_t *data = vdev->guest_ram + cur->addr - vdev->guest_ram_base;

            if (first) {
                /* Skip the virtio-net header */
                uint32_t hdr_size = sizeof(virtio_net_hdr_t);
                if (cur->len > hdr_size) {
                    uint32_t payload = cur->len - hdr_size;
                    if (pkt_len + payload <= sizeof(pkt_buf)) {
                        memcpy(pkt_buf + pkt_len, data + hdr_size, payload);
                        pkt_len += payload;
                    }
                }
                first = false;
            } else {
                /* Data descriptor */
                if (pkt_len + cur->len <= sizeof(pkt_buf)) {
                    memcpy(pkt_buf + pkt_len, data, cur->len);
                    pkt_len += cur->len;
                }
            }

            total_len += cur->len;

            if (cur->flags & VRING_DESC_F_NEXT) {
                cur = virtio_get_desc(vdev, queue_idx, cur->next);
            } else {
                cur = NULL;
            }
        }

        /* Send to host backend */
        if (pkt_len > 0 && net->backend.send) {
            net->backend.send(net->backend.opaque, pkt_buf, pkt_len);
        }

        virtio_put_used(vdev, queue_idx, head_idx, total_len);
    }

    virtio_raise_interrupt(vdev, VIRTIO_INT_VRING);
}

/* ── RX: Host → Guest ──────────────────────────── */

void virtio_net_poll_rx(virtio_net_t *net)
{
    virtio_device_t *vdev = &net->vdev;
    int queue_idx = 0;  /* receiveq */

    if (!net->backend.recv)
        return;

    /* Try to receive one packet per poll */
    uint8_t pkt_buf[65536];
    int pkt_len = net->backend.recv(net->backend.opaque, pkt_buf, sizeof(pkt_buf));
    if (pkt_len <= 0)
        return;

    /* Get an available descriptor from the guest */
    uint16_t head_idx;
    if (virtio_get_avail(vdev, queue_idx, &head_idx) != 0)
        return;  /* No buffers available */

    virtio_desc_t *desc = virtio_get_desc(vdev, queue_idx, head_idx);
    if (!desc) return;

    /* First descriptor: virtio-net header */
    uint8_t *hdr_data = vdev->guest_ram + desc->addr - vdev->guest_ram_base;
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t*)hdr_data;
    memset(hdr, 0, sizeof(*hdr));

    uint32_t written = sizeof(virtio_net_hdr_t);

    /* Remaining descriptors: packet data */
    virtio_desc_t *cur = desc;
    uint32_t pkt_offset = 0;

    /* First descriptor may also have space for data after header */
    if (desc->len > sizeof(virtio_net_hdr_t) && pkt_offset < (uint32_t)pkt_len) {
        uint32_t space = desc->len - sizeof(virtio_net_hdr_t);
        uint32_t copy = (uint32_t)pkt_len - pkt_offset;
        if (copy > space) copy = space;
        memcpy(hdr_data + sizeof(virtio_net_hdr_t), pkt_buf + pkt_offset, copy);
        pkt_offset += copy;
        written += copy;
    }

    /* Walk remaining descriptors */
    while ((cur->flags & VRING_DESC_F_NEXT) && pkt_offset < (uint32_t)pkt_len) {
        cur = virtio_get_desc(vdev, queue_idx, cur->next);
        if (!cur) break;

        uint8_t *data = vdev->guest_ram + cur->addr - vdev->guest_ram_base;
        uint32_t copy = (uint32_t)pkt_len - pkt_offset;
        if (copy > cur->len) copy = cur->len;
        memcpy(data, pkt_buf + pkt_offset, copy);
        pkt_offset += copy;
        written += copy;
    }

    /* If MRG_RXBUF, set num_buffers */
    hdr->num_buffers = 1;

    virtio_put_used(vdev, queue_idx, head_idx, written);
    virtio_raise_interrupt(vdev, VIRTIO_INT_VRING);
}

/* ── Queue Notification ────────────────────────── */

static void net_queue_notify(virtio_device_t *dev, int queue_idx)
{
    virtio_net_t *net = (virtio_net_t*)dev;

    if (queue_idx == 1) {
        /* TX queue */
        net_process_tx(net, 1);
    }
    /* RX is poll-driven via virtio_net_poll_rx */
}

static void net_reset(virtio_device_t *dev)
{
    (void)dev;
}

static void net_set_status(virtio_device_t *dev, uint32_t status)
{
    virtio_net_t *net = (virtio_net_t*)dev;
    if (status & VIRTIO_STATUS_DRIVER_OK) {
        net->config.status = VIRTIO_NET_S_LINK_UP;
    }
}

static const virtio_dev_ops_t net_ops = {
    .name = "virtio-net",
    .device_id = 1,          /* Virtio network device */
    .vendor_id = 0x554D4551, /* QEMU vendor */
    .get_features = net_get_features,
    .config_read = net_config_read,
    .config_write = net_config_write,
    .queue_notify = net_queue_notify,
    .reset = net_reset,
    .set_status = net_set_status,
};

/* ── Loopback Backend (default when no TAP) ────── */

static int loopback_send(void *opaque, const uint8_t *data, uint32_t len)
{
    /* Drop all packets (no real network) */
    (void)opaque; (void)data; (void)len;
    return (int)len;
}

static int loopback_recv(void *opaque, uint8_t *buf, uint32_t buf_len)
{
    (void)opaque; (void)buf; (void)buf_len;
    return 0;  /* No packets */
}

static const virtio_net_backend_t loopback_backend = {
    .send = loopback_send,
    .recv = loopback_recv,
    .opaque = NULL,
};

/* ── Public API ────────────────────────────────── */

int virtio_net_init(virtio_net_t *net, int irq_num,
                    uint8_t *guest_ram, uint64_t ram_base,
                    uint64_t ram_size)
{
    memset(net, 0, sizeof(*net));

    /* Default MAC: 02:00:00:00:00:01 */
    net->config.mac[0] = 0x02;
    net->config.mac[1] = 0x00;
    net->config.mac[2] = 0x00;
    net->config.mac[3] = 0x00;
    net->config.mac[4] = 0x00;
    net->config.mac[5] = 0x01;
    net->config.status = VIRTIO_NET_S_LINK_UP;
    net->config.max_virtqueue_pairs = 1;
    net->config.mtu = 1500;
    net->config.speed = 1000;  /* 1 Gbps */
    net->config.duplex = 1;    /* Full */

    snprintf(net->device_id, sizeof(net->device_id), "siliconv-net-0");

    /* Default backend: loopback (drop all) */
    net->backend = loopback_backend;

    /* 2 queues: 0=receiveq, 1=transmitq */
    virtio_init(&net->vdev, &net_ops, 2, irq_num,
                guest_ram, ram_base, ram_size);
    net->vdev.opaque = net;

    printf("virtio-net: initialized (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
           net->config.mac[0], net->config.mac[1], net->config.mac[2],
           net->config.mac[3], net->config.mac[4], net->config.mac[5]);

    return 0;
}

void virtio_net_destroy(virtio_net_t *net)
{
    (void)net;
}

void virtio_net_set_backend(virtio_net_t *net, const virtio_net_backend_t *backend)
{
    if (backend) {
        net->backend = *backend;
    }
}

void virtio_net_set_mac(virtio_net_t *net, const uint8_t mac[6])
{
    memcpy(net->config.mac, mac, 6);
}

virtio_device_t* virtio_net_get_vdev(virtio_net_t *net)
{
    return &net->vdev;
}
