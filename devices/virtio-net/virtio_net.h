/*
 * SiliconV — Virtio-Net Device
 *
 * Emulates a network device for the guest.
 * Minimal feature set for v0: MAC, STATUS, MTU.
 *
 * Reference: Virtio 1.2 Specification, Section 5.1
 */

#ifndef SILICONV_VIRTIO_NET_H
#define SILICONV_VIRTIO_NET_H

#include "../transport/virtio_mmio.h"
#include <stdio.h>
#include <stdbool.h>

/* ── Virtio-Net Feature Bits ───────────────────── */
#define VIRTIO_NET_F_CSUM           0   /* Host handles partial checksum */
#define VIRTIO_NET_F_GUEST_CSUM     1   /* Guest handles partial checksum */
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2
#define VIRTIO_NET_F_MAC            5   /* MAC address available */
#define VIRTIO_NET_F_GSO            6   /* Host handles GSO */
#define VIRTIO_NET_F_GUEST_TSO4     7
#define VIRTIO_NET_F_GUEST_TSO6     8
#define VIRTIO_NET_F_MRG_RXBUF      15  /* Merged RX buffers */
#define VIRTIO_NET_F_STATUS         16  /* Configuration status field */
#define VIRTIO_NET_F_CTRL_VQ        17  /* Control virtqueue available */
#define VIRTIO_NET_F_MQ             22  /* Multiple queue pairs */
#define VIRTIO_NET_F_MTU            23  /* Negotiable MTU */
#define VIRTIO_NET_F_SPEED_DUPLEX   63  /* Device reports speed/duplex */

/* ── Status Bits ───────────────────────────────── */
#define VIRTIO_NET_S_LINK_UP        1
#define VIRTIO_NET_S_ANNOUNCE       2

/* ── Header Flags ──────────────────────────────── */
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1
#define VIRTIO_NET_HDR_F_DATA_VALID 2
#define VIRTIO_NET_HDR_F_RSC_INFO   4

#define VIRTIO_NET_HDR_GSO_NONE     0
#define VIRTIO_NET_HDR_GSO_TCPV4    1
#define VIRTIO_NET_HDR_GSO_UDP      3
#define VIRTIO_NET_HDR_GSO_TCPV6    4
#define VIRTIO_NET_HDR_GSO_ECN      0x80

/* ── Packet Header (prepended to every packet) ─── */
typedef struct {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;    /* Only if MRG_RXBUF */
} __attribute__((packed)) virtio_net_hdr_t;

/* ── Config Space ──────────────────────────────── */
typedef struct {
    uint8_t  mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
    uint32_t speed;
    uint8_t  duplex;
} __attribute__((packed)) virtio_net_config_t;

/* ── Network Backend Interface ─────────────────── */
typedef struct {
    /* Send a packet to the host network */
    int  (*send)(void *opaque, const uint8_t *data, uint32_t len);
    /* Receive a packet from the host (non-blocking, returns bytes read or 0) */
    int  (*recv)(void *opaque, uint8_t *buf, uint32_t buf_len);
    void *opaque;
} virtio_net_backend_t;

/* ── Device State ──────────────────────────────── */
typedef struct {
    virtio_device_t vdev;         /* Must be first (polymorphism) */
    virtio_net_config_t config;
    virtio_net_backend_t backend; /* Host network backend */
    char device_id[256];
} virtio_net_t;

/* ── API ───────────────────────────────────────── */

/* Create a virtio-net device */
int virtio_net_init(virtio_net_t *net, int irq_num,
                    uint8_t *guest_ram, uint64_t ram_base,
                    uint64_t ram_size);

/* Destroy the device */
void virtio_net_destroy(virtio_net_t *net);

/* Set the network backend (TAP, user-mode, etc.) */
void virtio_net_set_backend(virtio_net_t *net, const virtio_net_backend_t *backend);

/* Set MAC address (default: 02:00:00:00:00:01) */
void virtio_net_set_mac(virtio_net_t *net, const uint8_t mac[6]);

/* Get the virtio device handle */
virtio_device_t* virtio_net_get_vdev(virtio_net_t *net);

/* Poll for incoming packets (call periodically from host) */
void virtio_net_poll_rx(virtio_net_t *net);

#endif /* SILICONV_VIRTIO_NET_H */
