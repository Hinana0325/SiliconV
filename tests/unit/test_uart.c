#include "pl011.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

typedef struct {
    char bytes[16];
    int count;
} tx_log_t;

static void tx_capture(uint8_t byte, void *opaque)
{
    tx_log_t *log = (tx_log_t *)opaque;
    if (log->count < (int)sizeof(log->bytes))
        log->bytes[log->count++] = (char)byte;
}

int main(void)
{
    pl011_state_t uart;
    tx_log_t log;
    memset(&log, 0, sizeof(log));

    pl011_init(&uart, 32);
    pl011_set_tx_callback(&uart, tx_capture, &log);

    CHECK((pl011_mmio_read(&uart, PL011_FR, 4) & PL011_FR_RXFE) != 0);
    pl011_mmio_write(&uart, PL011_DR, 'S', 4);
    pl011_mmio_write(&uart, PL011_DR, 'V', 4);
    CHECK(log.count == 2);
    CHECK(log.bytes[0] == 'S');
    CHECK(log.bytes[1] == 'V');

    pl011_rx_put(&uart, 'R');
    CHECK((pl011_mmio_read(&uart, PL011_FR, 4) & PL011_FR_RXFE) == 0);
    CHECK(pl011_mmio_read(&uart, PL011_DR, 4) == 'R');
    CHECK((pl011_mmio_read(&uart, PL011_FR, 4) & PL011_FR_RXFE) != 0);

    return 0;
}
