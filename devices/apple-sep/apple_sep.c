/*
 * SiliconV — Apple SEP (Secure Enclave Processor) — Simplified Implementation
 *
 * Minimal SEP emulation that provides enough functionality for
 * XNU to boot. Handles:
 *   - Mailbox interface (message passing)
 *   - TRNG (random number generation)
 *   - Basic state machine (SLEEPING → ACTIVE)
 *
 * This is NOT a full SEP implementation — it's a stub that
 * returns success to common SEP queries without real crypto.
 */

#include "apple_sep.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void apple_sep_init(apple_sep_state_t *sep, int irq_num)
{
    memset(sep, 0, sizeof(*sep));

    sep->state = SEP_STATE_SLEEPING;
    sep->irq_num = irq_num;
    sep->mbox_status = 0;
    sep->trng_val = 0;
    sep->trng_ctrl = 0;
}

void apple_sep_set_irq_ctx(apple_sep_state_t *sep, void *ctx)
{
    sep->irq_context = ctx;
}

void apple_sep_set_irq_callbacks(apple_sep_state_t *sep,
                                  void (*raise)(void *ctx, int irq),
                                  void (*lower)(void *ctx, int irq))
{
    sep->irq_raise = raise;
    sep->irq_lower = lower;
}

/* ── Handle SEP mailbox message ─────────────────── */
static void sep_handle_message(apple_sep_state_t *sep)
{
    uint64_t msg = sep->mbox_msg_in;
    uint64_t response = 0;

    /* Decode message type from upper bits */
    uint32_t msg_type = (uint32_t)(msg >> 32);
    uint32_t msg_data = (uint32_t)(msg & 0xFFFFFFFF);

    switch (msg_type) {
    case 0x00000001: /* Ping / hello */
        response = 0x0000000100000001ULL; /* Ack */
        printf("sep: ping\n");
        break;

    case 0x00000002: /* Get status */
        response = 0x0000000200000000ULL | (uint32_t)sep->state;
        printf("sep: status query → state=%d\n", sep->state);
        break;

    case 0x00000003: /* Boot request */
        if (sep->state == SEP_STATE_SLEEPING) {
            sep->state = SEP_STATE_BOOTSTRAP;
            response = 0x0000000300000000ULL; /* Boot started */
            printf("sep: bootstrap initiated\n");

            /* Fast-forward to active */
            sep->state = SEP_STATE_ACTIVE;
            response = 0x0000000300000001ULL; /* Boot complete */
            printf("sep: active\n");
        }
        break;

    case 0x00000004: /* Get random bytes */
        /* Return pseudo-random data */
        sep->trng_val = (uint32_t)(rand() ^ (uint32_t)(uintptr_t)sep);
        response = 0x0000000400000000ULL | sep->trng_val;
        break;

    case 0x00000005: /* Key operation (stub) */
        /* Always succeed */
        response = 0x0000000500000000ULL;
        break;

    case 0x00000006: /* Crypto operation (stub) */
        response = 0x0000000600000000ULL;
        break;

    default:
        printf("sep: unknown message type 0x%08X (data=0x%08X)\n",
               msg_type, msg_data);
        response = msg; /* Echo back */
        break;
    }

    sep->mbox_msg_out = response;
    sep->mbox_status |= 1; /* Response ready */

    /* Raise IRQ to signal SEP response available */
    if (sep->irq_raise)
        sep->irq_raise(sep->irq_context, sep->irq_num);
}

void apple_sep_tick(apple_sep_state_t *sep)
{
    /* If there's a pending message, process it */
    if (sep->mbox_ctrl & 1) {
        sep_handle_message(sep);
        sep->mbox_ctrl &= ~1; /* Clear pending */
    }
}

/* ── MMIO Read ──────────────────────────────────── */
uint64_t apple_sep_mmio_read(apple_sep_state_t *sep, uint64_t offset, int size)
{
    (void)size;

    /* Mailbox region */
    if (offset < SEP_REGION_PMGR) {
        switch (offset) {
        case SEP_MBOX_CTRL:     return sep->mbox_ctrl;
        case SEP_MBOX_MSG_IN:   return sep->mbox_msg_in;
        case SEP_MBOX_MSG_OUT:  return sep->mbox_msg_out;
        case SEP_MBOX_STATUS:   return sep->mbox_status;
        case SEP_MBOX_IRQ_CTRL: return sep->mbox_irq_ctrl;
        default:                return 0;
        }
    }

    /* Mailbox: reading msg_out clears IRQ */
    if (offset == SEP_MBOX_MSG_OUT && sep->mbox_status & 1) {
        sep->mbox_status &= ~1;
        if (sep->irq_lower)
            sep->irq_lower(sep->irq_context, sep->irq_num);
    }

    /* TRNG region */
    if (offset >= SEP_REGION_TRNG && offset < SEP_REGION_TRNG + 0x1000) {
        switch (offset - SEP_REGION_TRNG) {
        case 0x00: return sep->trng_ctrl;
        case 0x04: return sep->trng_val;
        default:   return 0;
        }
    }

    /* SRAM region */
    if (offset >= SEP_REGION_SRAM && offset < SEP_REGION_SRAM + sizeof(sep->sram)) {
        uint64_t sram_off = offset - SEP_REGION_SRAM;
        uint64_t val = 0;
        memcpy(&val, sep->sram + sram_off, size < 8 ? size : 8);
        return val;
    }

    return 0;
}

/* ── MMIO Write ─────────────────────────────────── */
void apple_sep_mmio_write(apple_sep_state_t *sep, uint64_t offset,
                           uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)(value & 0xFFFFFFFF);

    if (offset < SEP_REGION_PMGR) {
        switch (offset) {
        case SEP_MBOX_CTRL:
            sep->mbox_ctrl = v;
            if (v & 1) {
                sep->mbox_status = 0; /* Clear status */
                apple_sep_tick(sep);
            }
            break;
        case SEP_MBOX_MSG_IN:
            sep->mbox_msg_in = value;
            break;
        case SEP_MBOX_IRQ_CTRL:
            sep->mbox_irq_ctrl = v;
            break;
        }
        return;
    }

    /* SRAM region */
    if (offset >= SEP_REGION_SRAM && offset < SEP_REGION_SRAM + sizeof(sep->sram)) {
        uint64_t sram_off = offset - SEP_REGION_SRAM;
        memcpy(sep->sram + sram_off, &value, size < 8 ? size : 8);
        return;
    }
}
