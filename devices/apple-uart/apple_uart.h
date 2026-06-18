/*
 * SiliconV — Apple S5L UART Emulation
 *
 * Implements Samsung S5L UART as used in Apple Silicon SoCs.
 * Used by iBoot and XNU for serial console output.
 *
 * This replaces PL011 in the Apple platform profile.
 *
 * Register layout is similar to Exynos 4210 UART.
 */

#ifndef SILICONV_APPLE_UART_H
#define SILICONV_APPLE_UART_H

#include <stdint.h>
#include <stdbool.h>

/* ── Register Offsets ──────────────────────────── */
#define S5L_ULCON    0x00    /* Line Control */
#define S5L_UCON     0x04    /* Control */
#define S5L_UFCON    0x08    /* FIFO Control */
#define S5L_UMCON    0x0C    /* Modem Control */
#define S5L_UTRSTAT  0x10    /* TX/RX Status */
#define S5L_UERSTAT  0x14    /* Error Status */
#define S5L_UFSTAT   0x18    /* FIFO Status */
#define S5L_UMSTAT   0x1C    /* Modem Status */
#define S5L_UTXH     0x20    /* TX Holding Register */
#define S5L_URXH     0x24    /* RX Holding Register */
#define S5L_UBRDIV   0x28    /* Baud Rate Divisor */
#define S5L_UDIVSLOT 0x2C    /* Baud Rate Divisor Slot */
#define S5L_UINTP    0x30    /* Interrupt Pending */
#define S5L_UINTM    0x34    /* Interrupt Mask */
#define S5L_UINTCH   0x38    /* Interrupt Channel Select */
#define S5L_UINTF    0x3C    /* Interrupt Filter Control */

/* ── ULCON bits ───────────────────────────────── */
#define S5L_ULCON_STOP_BIT    (1 << 1)
#define S5L_ULCON_PARITY_EN   (1 << 3)
#define S5L_ULCON_PARITY_MASK (0x7 << 3)
#define S5L_ULCON_DATA_BITS   (0x3)    /* 0=5bit, 1=6bit, 2=7bit, 3=8bit */

/* ── UCON bits ────────────────────────────────── */
#define S5L_UCON_RX_MODE_MASK (0x3 << 0)  /* 0=poll, 1=IRQ, 2=DMA */
#define S5L_UCON_TX_MODE_MASK (0x3 << 2)  /* 0=poll, 1=IRQ, 2=DMA */
#define S5L_UCON_SEND_BREAK   (1 << 4)
#define S5L_UCON_LOOPBACK     (1 << 5)
#define S5L_UCON_RX_IRQ_EN    (1 << 6)
#define S5L_UCON_TX_IRQ_EN    (1 << 7)
#define S5L_UCON_RX_TIMEOUT   (1 << 7)    /* Different bit on some models */

/* ── UTRSTAT bits ──────────────────────────────── */
#define S5L_UTRSTAT_TX_EMPTY    (1 << 2)
#define S5L_UTRSTAT_TX_READY    (1 << 1)
#define S5L_UTRSTAT_RX_READY    (1 << 0)

/* ── UFSTAT bits ───────────────────────────────── */
#define S5L_UFSTAT_TX_FULL    (1 << 9)
#define S5L_UFSTAT_TX_COUNT   (0xFF << 0)
#define S5L_UFSTAT_RX_FULL    (1 << 24)
#define S5L_UFSTAT_RX_COUNT   (0xFF << 16)

/* ── UINTP bits ───────────────────────────────── */
#define S5L_INT_TX      (1 << 0)    /* TX done */
#define S5L_INT_RX      (1 << 1)    /* RX available */
#define S5L_INT_ERROR   (1 << 2)    /* Error */
#define S5L_INT_TIMEOUT (1 << 3)    /* RX timeout */

/* ── FIFO Sizes ────────────────────────────────── */
#define APPLE_UART_TX_FIFO_SIZE  256
#define APPLE_UART_RX_FIFO_SIZE  256

/* ── State ─────────────────────────────────────── */
typedef struct {
    /* Registers */
    uint32_t ulcon;
    uint32_t ucon;
    uint32_t ufcon;
    uint32_t umcon;
    uint32_t ubrdiv;
    uint32_t udivslot;
    uint32_t uintp;
    uint32_t uintm;
    uint32_t uintch;
    uint32_t uintf;

    /* FIFOs */
    uint8_t  tx_fifo[APPLE_UART_TX_FIFO_SIZE];
    uint16_t tx_head;
    uint16_t tx_count;

    uint8_t  rx_fifo[APPLE_UART_RX_FIFO_SIZE];
    uint16_t rx_head;
    uint16_t rx_count;

    /* TX callback (for output to host console) */
    void (*tx_callback)(uint8_t byte, void *opaque);
    void   *tx_opaque;

    /* IRQ output — AIC or other interrupt controller */
    void    *irq_context;       /* AIC state pointer */
    void    (*irq_raise)(void *ctx, int irq);  /* Function to raise IRQ */
    void    (*irq_lower)(void *ctx, int irq);  /* Function to lower IRQ */
    int      irq_num;

    /* ID for debug output */
    int     uart_id;
} apple_uart_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize Apple UART */
void apple_uart_init(apple_uart_state_t *uart, int irq_num, int uart_id);

/* Set TX callback (guest output → host console) */
void apple_uart_set_tx_callback(apple_uart_state_t *uart,
                                 void (*cb)(uint8_t, void*),
                                 void *opaque);

/* Set IRQ context (for raising interrupts) */
void apple_uart_set_irq_ctx(apple_uart_state_t *uart, void *ctx);

/* Set IRQ raise/lower callbacks (wire to AIC) */
void apple_uart_set_irq_callbacks(apple_uart_state_t *uart,
                                   void (*raise)(void *ctx, int irq),
                                   void (*lower)(void *ctx, int irq));

/* Inject RX data (host → guest) */
void apple_uart_rx_put(apple_uart_state_t *uart, uint8_t byte);

/* Update IRQ output */
void apple_uart_update_irq(apple_uart_state_t *uart);

/* MMIO handlers */
uint64_t apple_uart_mmio_read(apple_uart_state_t *uart, uint64_t offset, int size);
void     apple_uart_mmio_write(apple_uart_state_t *uart, uint64_t offset,
                                uint64_t value, int size);

#endif /* SILICONV_APPLE_UART_H */
