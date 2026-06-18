/*
 * SiliconV — Apple S5L UART Emulation (Implementation)
 *
 * Samsung S5L UART as used in Apple Silicon SoCs.
 * Provides serial console output for iBoot and XNU.
 *
 * Replaces PL011 in the Apple platform profile.
 */

#include "apple_uart.h"
#include <string.h>
#include <stdio.h>

void apple_uart_init(apple_uart_state_t *uart, int irq_num, int uart_id)
{
    memset(uart, 0, sizeof(*uart));

    uart->irq_num = irq_num;
    uart->uart_id = uart_id;

    /* Default config: 8N1, poll mode */
    uart->ulcon = 0x3;               /* 8-bit data */
    uart->ucon = 0x0;                /* Polling mode */
    uart->ufcon = 0x0;               /* FIFO disabled */
    uart->umcon = 0x0;
    uart->uintm = 0x0F;              /* All interrupts masked by default */

    /* TX/RX FIFOs initially empty */
    uart->tx_head = 0;
    uart->tx_count = 0;
    uart->rx_head = 0;
    uart->rx_count = 0;
}

void apple_uart_set_tx_callback(apple_uart_state_t *uart,
                                 void (*cb)(uint8_t, void*),
                                 void *opaque)
{
    uart->tx_callback = cb;
    uart->tx_opaque = opaque;
}

void apple_uart_set_irq_ctx(apple_uart_state_t *uart, void *ctx)
{
    uart->irq_context = ctx;
}

void apple_uart_set_irq_callbacks(apple_uart_state_t *uart,
                                   void (*raise)(void *ctx, int irq),
                                   void (*lower)(void *ctx, int irq))
{
    uart->irq_raise = raise;
    uart->irq_lower = lower;
}

void apple_uart_rx_put(apple_uart_state_t *uart, uint8_t byte)
{
    if (uart->rx_count >= APPLE_UART_RX_FIFO_SIZE)
        return; /* FIFO full, drop */

    int idx = (uart->rx_head + uart->rx_count) % APPLE_UART_RX_FIFO_SIZE;
    uart->rx_fifo[idx] = byte;
    uart->rx_count++;

    /* Set RX pending interrupt */
    uart->uintp |= S5L_INT_RX;
    apple_uart_update_irq(uart);
}

/* ── Internal: flush TX FIFO to callback ────────── */
static void apple_uart_flush_tx(apple_uart_state_t *uart)
{
    while (uart->tx_count > 0) {
        uint8_t byte = uart->tx_fifo[uart->tx_head];
        uart->tx_head = (uart->tx_head + 1) % APPLE_UART_TX_FIFO_SIZE;
        uart->tx_count--;

        if (uart->tx_callback) {
            uart->tx_callback(byte, uart->tx_opaque);
        }
    }
}

/* ── Update IRQ output ──────────────────────────── */
void apple_uart_update_irq(apple_uart_state_t *uart)
{
    /* Any unmasked pending interrupt? */
    uint32_t pending = uart->uintp & ~uart->uintm;

    /* Also consider TX ready if TX IRQ enabled */
    if (uart->tx_count < APPLE_UART_TX_FIFO_SIZE &&
        (uart->ucon & S5L_UCON_TX_IRQ_EN)) {
        pending |= S5L_INT_TX;
    }

    /* Signal IRQ through wired callbacks (to AIC) */
    if (pending) {
        if (uart->irq_raise)
            uart->irq_raise(uart->irq_context, uart->irq_num);
    } else {
        if (uart->irq_lower)
            uart->irq_lower(uart->irq_context, uart->irq_num);
    }
}

/* ── MMIO Read ──────────────────────────────────── */
uint64_t apple_uart_mmio_read(apple_uart_state_t *uart, uint64_t offset, int size)
{
    (void)size;

    switch (offset) {
    case S5L_ULCON:
        return uart->ulcon;

    case S5L_UCON:
        return uart->ucon;

    case S5L_UFCON:
        return uart->ufcon;

    case S5L_UMCON:
        return uart->umcon;

    case S5L_UTRSTAT: {
        uint32_t stat = 0;
        if (uart->tx_count == 0)
            stat |= S5L_UTRSTAT_TX_EMPTY;
        if (uart->tx_count < APPLE_UART_TX_FIFO_SIZE)
            stat |= S5L_UTRSTAT_TX_READY;
        if (uart->rx_count > 0)
            stat |= S5L_UTRSTAT_RX_READY;
        return stat;
    }

    case S5L_UERSTAT:
        return 0; /* No errors */

    case S5L_UFSTAT: {
        uint32_t stat = 0;
        stat |= (uart->tx_count >= APPLE_UART_TX_FIFO_SIZE) ? S5L_UFSTAT_TX_FULL : 0;
        stat |= (uart->tx_count & 0xFF);
        stat |= (uart->rx_count >= APPLE_UART_RX_FIFO_SIZE) ? S5L_UFSTAT_RX_FULL : 0;
        stat |= ((uint32_t)uart->rx_count & 0xFF) << 16;
        return stat;
    }

    case S5L_UMSTAT:
        return 0;

    case S5L_UTXH:
        /* Reading TX holding reg — return 0 (write-only normally) */
        return 0;

    case S5L_URXH: {
        /* Pop byte from RX FIFO */
        if (uart->rx_count > 0) {
            uint8_t byte = uart->rx_fifo[uart->rx_head];
            uart->rx_head = (uart->rx_head + 1) % APPLE_UART_RX_FIFO_SIZE;
            uart->rx_count--;
            if (uart->rx_count == 0)
                uart->uintp &= ~S5L_INT_RX;
            apple_uart_update_irq(uart);
            return byte;
        }
        return 0;
    }

    case S5L_UBRDIV:
        return uart->ubrdiv;

    case S5L_UDIVSLOT:
        return uart->udivslot;

    case S5L_UINTP:
        return uart->uintp;

    case S5L_UINTM:
        return uart->uintm;

    case S5L_UINTCH:
        return uart->uintch;

    case S5L_UINTF:
        return uart->uintf;

    default:
        return 0;
    }
}

/* ── MMIO Write ─────────────────────────────────── */
void apple_uart_mmio_write(apple_uart_state_t *uart, uint64_t offset,
                            uint64_t value, int size)
{
    (void)size;
    uint32_t v = value & 0xFFFFFFFF;

    switch (offset) {
    case S5L_ULCON:
        uart->ulcon = v & 0xFF;
        break;

    case S5L_UCON:
        uart->ucon = v & 0xFF;
        break;

    case S5L_UFCON:
        uart->ufcon = v & 0xFF;
        /* Bit 0 = FIFO enable, bits 1-3 = RX FIFO trigger, bits 4-6 = TX FIFO trigger */
        break;

    case S5L_UMCON:
        uart->umcon = v & 0xFF;
        break;

    case S5L_UTXH: {
        /* Write byte to TX FIFO */
        if (uart->tx_count < APPLE_UART_TX_FIFO_SIZE) {
            uint8_t byte = v & 0xFF;
            int idx = (uart->tx_head + uart->tx_count) % APPLE_UART_TX_FIFO_SIZE;
            uart->tx_fifo[idx] = byte;
            uart->tx_count++;
            apple_uart_flush_tx(uart);
        }
        break;
    }

    case S5L_UBRDIV:
        uart->ubrdiv = v & 0xFFFF;
        break;

    case S5L_UDIVSLOT:
        uart->udivslot = v & 0xFFFF;
        break;

    case S5L_UINTP:
        /* Clear pending interrupts by writing 1 to the bit */
        uart->uintp &= ~v;
        break;

    case S5L_UINTM:
        uart->uintm = v & 0x0F;
        apple_uart_update_irq(uart);
        break;

    case S5L_UINTCH:
        uart->uintch = v & 0x3;
        break;

    case S5L_UINTF:
        uart->uintf = v;
        break;

    default:
        break;
    }
}
