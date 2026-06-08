/*
 * SiliconV — TAP Network Backend
 *
 * Bridges virtio-net to a Linux TAP device for real network connectivity.
 * Requires root or CAP_NET_ADMIN.
 */

#ifndef SILICONV_TAP_BACKEND_H
#define SILICONV_TAP_BACKEND_H

#include "../../devices/virtio-net/virtio_net.h"

/* Create a TAP backend and attach it to a virtio-net device.
 * tap_name: "tap0" or NULL for auto-assign
 * Returns 0 on success, -1 on error.
 */
int tap_backend_create(virtio_net_t *net, const char *tap_name);

/* Destroy the TAP backend */
void tap_backend_destroy(virtio_net_t *net);

/* Get the file descriptor (for poll/select) */
int tap_backend_fd(virtio_net_t *net);

#endif /* SILICONV_TAP_BACKEND_H */
