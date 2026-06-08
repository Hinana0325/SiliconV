/*
 * SiliconV — PL011 UART Emulation (Implementation)
 *
 * Minimal PL011 implementation for guest serial console.
 * Handles register reads/writes and TX/RX FIFOs.
 */

#include "pl011.h"
#include <string.h>
#include <stdio.h>
#include "../../core/irq/gic.h"

void pl011_init(pl011_state_t *uart, int irq_num)
{
    memset(uart, 0, sizeof(*uart));

    uart->irq_num = irq_num;
    uart->cr = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
    /* FR is derived from FIFO state in pl011_mmio_read, not stored */
    uart->tx_head = 0;
    uart->tx_count = 0;
    uart->rx_head = 0;
    uart->rx_count = 0;
}

void pl011_set_tx_callback(pl011_state_t *uart,
                           void (*cb)(uint8_t, void*),
                           void *opaque)
{
    uart->tx_callback = cb;
    uart->tx_opaque = opaque;
}

void pl011_set_gic(pl011_state_t *uart, void *gic)
{
    uart->gic = gic;
}

static void pl011_update_irq(pl011_state_t *uart);

void pl011_rx_put(pl011_state_t *uart, uint8_t byte)
{
    if (uart->rx_count >= 256)
        return;  /* FIFO full, drop */

    int idx = (uart->rx_head + uart->rx_count) % 256;
    uart->rx_fifo[idx] = byte;
    uart->rx_count++;

    /* Raise RX interrupt if not masked */
    uart->ris |= PL011_INT_RX;
    pl011_update_irq(uart);
}

static void pl011_update_irq(pl011_state_t *uart)
{
    /* MIS = RIS & ~IMSC */
    uint32_t mis = uart->ris & ~uart->imsc;
    gic_state_t *gic = (gic_state_t*)uart->gic;

    if (!gic)
        return;

    if (mis)
        gic_raise_spi(gic, uart->irq_num);
    else
        gic_lower_spi(gic, uart->irq_num);
}

uint64_t pl011_mmio_read(pl011_state_t *uart, uint64_t offset, int size)
{
    (void)size;

    switch (offset) {
    case PL011_DR: {
        /* Data Register read — pop from RX FIFO */
        uint32_t val = 0;
        if (uart->rx_count > 0) {
            val = uart->rx_fifo[uart->rx_head];
            uart->rx_head = (uart->rx_head + 1) % 256;
            uart->rx_count--;
            if (uart->rx_count == 0)
                uart->ris &= ~PL011_INT_RX;
            pl011_update_irq(uart);
        }
        /* Also include error flags from RSR */
        val |= (uart->rsr << 8);
        return val;
    }

    case PL011_RSR:
        return uart->rsr;

    case PL011_FR: {
        uint32_t fr = 0;
        if (uart->tx_count >= 256) fr |= PL011_FR_TXFF;
        if (uart->tx_count == 0)   fr |= PL011_FR_TXFE;
        if (uart->rx_count == 0)   fr |= PL011_FR_RXFE;
        if (uart->rx_count >= 256) fr |= PL011_FR_RXFF;
        /* TX not busy when UART enabled and TX enabled */
        if (!(uart->cr & PL011_CR_UARTEN) || !(uart->cr & PL011_CR_TXE))
            fr |= PL011_FR_TXFE;
        return fr;
    }

    case PL011_IBRD:
        return uart->ibrd;

    case PL011_FBRD:
        return uart->fbrd;

    case PL011_LCR_H:
        return uart->lcr_h;

    case PL011_CR:
        return uart->cr;

    case PL011_IFLS:
        return uart->ifls;

    case PL011_IMSC:
        return uart->imsc;

    case PL011_RIS:
        return uart->ris;

    case PL011_MIS:
        return uart->ris & ~uart->imsc;

    default:
        return 0;
    }
}

void pl011_mmio_write(pl011_state_t *uart, uint64_t offset,
                      uint64_t value, int size)
{
    (void)size;

    switch (offset) {
    case PL011_DR: {
        /* Data Register write — push to TX FIFO */
        uint8_t byte = value & 0xff;
        if (uart->tx_callback) {
            uart->tx_callback(byte, uart->tx_opaque);
        }
        /* Set TX interrupt */
        uart->ris |= PL011_INT_TX;
        pl011_update_irq(uart);
        break;
    }

    case PL011_RSR:
        uart->rsr = value & 0xf;
        break;

    case PL011_IBRD:
        uart->ibrd = value & 0xffff;
        break;

    case PL011_FBRD:
        uart->fbrd = value & 0x3f;
        break;

    case PL011_LCR_H:
        uart->lcr_h = value & 0xff;
        break;

    case PL011_CR:
        uart->cr = value & 0x7ff;
        break;

    case PL011_IFLS:
        uart->ifls = value & 0x3f;
        break;

    case PL011_IMSC:
        uart->imsc = value & 0x7ff;
        pl011_update_irq(uart);
        break;

    case PL011_ICR:
        /* Writing 1s clears the corresponding RIS bits */
        uart->ris &= ~(value & 0x7ff);
        pl011_update_irq(uart);
        break;

    default:
        /* Ignore writes to unknown registers */
        break;
    }
}
