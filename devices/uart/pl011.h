/*
 * SiliconV — PL011 UART Emulation
 *
 * Emulates ARM PL011 UART for guest serial console.
 * Implements the minimal register set needed for early boot output.
 *
 * MMIO Base: 0x10000000 (from spec/memory/mmio-map.md)
 * IRQ: 32 (from spec/irq/irq-map.md)
 */

#ifndef SILICONV_UART_H
#define SILICONV_UART_H

#include <stdint.h>

/* PL011 Register Offsets */
#define PL011_DR        0x000   /* Data Register */
#define PL011_RSR       0x004   /* Receive Status */
#define PL011_FR        0x018   /* Flag Register */
#define PL011_ILPR      0x020   /* IrDA Low-Power */
#define PL011_IBRD      0x024   /* Integer Baud Rate */
#define PL011_FBRD      0x028   /* Fractional Baud Rate */
#define PL011_LCR_H     0x02C   /* Line Control */
#define PL011_CR        0x030   /* Control Register */
#define PL011_IFLS      0x034   /* Interrupt FIFO Level */
#define PL011_IMSC      0x038   /* Interrupt Mask */
#define PL011_RIS       0x03C   /* Raw Interrupt Status */
#define PL011_MIS       0x040   /* Masked Interrupt Status */
#define PL011_ICR       0x044   /* Interrupt Clear */

/* Flag Register bits */
#define PL011_FR_BUSY   (1 << 3)
#define PL011_FR_RXFE   (1 << 4)  /* RX FIFO Empty */
#define PL011_FR_TXFF   (1 << 5)  /* TX FIFO Full */
#define PL011_FR_RXFF   (1 << 6)  /* RX FIFO Full */
#define PL011_FR_TXFE   (1 << 7)  /* TX FIFO Empty */

/* Control Register bits */
#define PL011_CR_UARTEN (1 << 0)
#define PL011_CR_TXE    (1 << 8)
#define PL011_CR_RXE    (1 << 9)

/* Interrupt bits */
#define PL011_INT_TX    (1 << 5)  /* TX interrupt */
#define PL011_INT_RX    (1 << 4)  /* RX interrupt */

/* UART state */
typedef struct {
    uint32_t cr;        /* Control Register */
    uint32_t lcr_h;     /* Line Control */
    uint32_t ifls;      /* FIFO Level Select */
    uint32_t imsc;      /* Interrupt Mask */
    uint32_t ris;       /* Raw Interrupt Status */
    uint32_t ibrd;      /* Integer Baud Rate */
    uint32_t fbrd;      /* Fractional Baud Rate */
    uint32_t rsr;       /* Receive Status */

    /* FIFO */
    uint8_t  tx_fifo[256];
    int      tx_head;
    int      tx_count;

    uint8_t  rx_fifo[256];
    int      rx_head;
    int      rx_count;

    /* Callback for TX output */
    void (*tx_callback)(uint8_t byte, void *opaque);
    void *tx_opaque;

    /* IRQ state */
    int      irq_num;
    void    *gic;       /* Opaque GIC reference for raising IRQ */
} pl011_state_t;

/* Initialize UART state */
void pl011_init(pl011_state_t *uart, int irq_num);

/* Set TX callback (called when guest writes a byte) */
void pl011_set_tx_callback(pl011_state_t *uart,
                           void (*cb)(uint8_t, void*),
                           void *opaque);

/* Attach the UART to a GIC instance for IRQ delivery */
void pl011_set_gic(pl011_state_t *uart, void *gic);

/* Inject a byte into RX FIFO (host → guest) */
void pl011_rx_put(pl011_state_t *uart, uint8_t byte);

/* MMIO read handler — called by hypervisor on guest MMIO read */
uint64_t pl011_mmio_read(pl011_state_t *uart, uint64_t offset, int size);

/* MMIO write handler — called by hypervisor on guest MMIO write */
void pl011_mmio_write(pl011_state_t *uart, uint64_t offset,
                      uint64_t value, int size);

#endif /* SILICONV_UART_H */
