/*
 * SiliconV — Apple SEP (Secure Enclave Processor) — Simplified Emulation
 *
 * Implements a minimal SEP emulation for the Apple virtual platform.
 * This simplified version handles the SEP mailbox interface and
 * responds to basic requests without requiring real SEP firmware.
 *
 * The full SEP (5673 lines in Inferno) is not implemented here.
 * This stub provides enough functionality for XNU to boot past
 * SEP initialization.
 */

#ifndef SILICONV_APPLE_SEP_H
#define SILICONV_APPLE_SEP_H

#include <stdint.h>
#include <stdbool.h>

/* ── SEP MMIO Regions ──────────────────────────── */
#define SEP_REGION_MBOX        0x00000     /* Mailbox (4KB) */
#define SEP_REGION_PMGR        0x10000     /* Power management */
#define SEP_REGION_TRNG        0x20000     /* Random number generator */
#define SEP_REGION_KEY         0x30000     /* Key storage */
#define SEP_REGION_AESS        0x40000     /* AES engine */
#define SEP_REGION_PKA         0x50000     /* Public key accelerator */
#define SEP_REGION_MONI        0x60000     /* Monitor */
#define SEP_REGION_SRAM        0x70000     /* SEP SRAM (large) */

/* ── Mailbox registers ─────────────────────────── */
#define SEP_MBOX_CTRL          0x00
#define SEP_MBOX_MSG_IN        0x10
#define SEP_MBOX_MSG_OUT       0x20
#define SEP_MBOX_STATUS        0x30
#define SEP_MBOX_IRQ_CTRL      0x40

/* ── SEP states ────────────────────────────────── */
typedef enum {
    SEP_STATE_SLEEPING = 0,
    SEP_STATE_BOOTSTRAP,
    SEP_STATE_ACTIVE,
    SEP_STATE_ERROR,
} sep_state_t;

/* ── SEP State ─────────────────────────────────── */
typedef struct {
    sep_state_t state;

    /* Mailbox */
    uint64_t mbox_msg_in;
    uint64_t mbox_msg_out;
    uint32_t mbox_ctrl;
    uint32_t mbox_status;
    uint32_t mbox_irq_ctrl;

    /* TRNG state */
    uint32_t trng_val;
    uint32_t trng_ctrl;

    /* SRAM */
    uint8_t  sram[0x1000];      /* 4KB SEP SRAM */
    uint32_t sram_addr;

    /* IRQ */
    void    *irq_context;
    void    (*irq_raise)(void *ctx, int irq);
    void    (*irq_lower)(void *ctx, int irq);
    int      irq_num;
} apple_sep_state_t;

/* ── API ───────────────────────────────────────── */
void apple_sep_init(apple_sep_state_t *sep, int irq_num);
void apple_sep_set_irq_ctx(apple_sep_state_t *sep, void *ctx);
void apple_sep_set_irq_callbacks(apple_sep_state_t *sep,
                                  void (*raise)(void *ctx, int irq),
                                  void (*lower)(void *ctx, int irq));

/* Tick SEP state machine */
void apple_sep_tick(apple_sep_state_t *sep);

/* MMIO handlers */
uint64_t apple_sep_mmio_read(apple_sep_state_t *sep, uint64_t offset, int size);
void     apple_sep_mmio_write(apple_sep_state_t *sep, uint64_t offset,
                               uint64_t value, int size);

#endif /* SILICONV_APPLE_SEP_H */
