/*
 * SiliconV — TAP Network Backend (Implementation)
 *
 * Creates a TAP device and bridges it to virtio-net.
 * The TAP device appears as a network interface on the host
 * and can be bridged with other interfaces for full connectivity.
 */

#include "tap_backend.h"

#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

typedef struct {
    int fd;
    char name[IFNAMSIZ];
} tap_state_t;

/* ── TAP Backend Ops ───────────────────────────── */

static int tap_send(void *opaque, const uint8_t *data, uint32_t len)
{
    tap_state_t *tap = (tap_state_t *)opaque;
    if (tap->fd < 0) return -1;

    ssize_t n = write(tap->fd, data, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        perror("tap: write");
        return -1;
    }
    return (int)n;
}

static int tap_recv(void *opaque, uint8_t *buf, uint32_t buf_len)
{
    tap_state_t *tap = (tap_state_t *)opaque;
    if (tap->fd < 0) return 0;

    /* Non-blocking read */
    ssize_t n = read(tap->fd, buf, buf_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        perror("tap: read");
        return 0;
    }
    return (int)n;
}

/* ── Public API ────────────────────────────────── */

int tap_backend_create(virtio_net_t *net, const char *tap_name)
{
    tap_state_t *tap = calloc(1, sizeof(tap_state_t));
    if (!tap) return -1;

    /* Open TAP device */
    tap->fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (tap->fd < 0) {
        fprintf(stderr, "tap: cannot open /dev/net/tun: %s\n", strerror(errno));
        free(tap);
        return -1;
    }

    /* Configure TAP device */
    struct ifreq ifr = {0};
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (tap_name) {
        strncpy(ifr.ifr_name, tap_name, IFNAMSIZ - 1);
    }

    if (ioctl(tap->fd, TUNSETIFF, &ifr) < 0) {
        fprintf(stderr, "tap: TUNSETIFF failed: %s\n", strerror(errno));
        close(tap->fd);
        free(tap);
        return -1;
    }

    strncpy(tap->name, ifr.ifr_name, IFNAMSIZ);

    /* Set non-blocking */
    int flags = fcntl(tap->fd, F_GETFL, 0);
    fcntl(tap->fd, F_SETFL, flags | O_NONBLOCK);

    /* Set up backend ops */
    virtio_net_backend_t backend = {
        .send = tap_send,
        .recv = tap_recv,
        .opaque = tap,
    };
    virtio_net_set_backend(net, &backend);

    printf("tap: created %s (fd=%d)\n", tap->name, tap->fd);
    return 0;
}

void tap_backend_destroy(virtio_net_t *net)
{
    /* The backend opaque pointer is our tap_state_t */
    /* We'd need to store a reference to free it properly */
    (void)net;
}

int tap_backend_fd(virtio_net_t *net)
{
    (void)net;
    /* Would need to store reference to tap_state_t */
    return -1;
}

#endif /* __linux__ */
