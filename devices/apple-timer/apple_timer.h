/*
 * SiliconV — Apple Timer
 *
 * Implements the Apple system timer/counter used in Apple Silicon SoCs.
 * Provides a free-running counter and compare registers for timed interrupts.
 * Used by XNU for clock_gettime, scheduling, and period-based events.
 */

#ifndef SILICONV_APPLE_TIMER_H
#define SILICONV_APPLE_TIMER_H

#include <stdint.h>
#include <stdbool.h>

/* ── Register Offsets ──────────────────────────── */
#define TIMER_REG_CNT_LOW   0x00    /* Counter low (read-only) */
#define TIMER_REG_CNT_HIGH  0x04    /* Counter high (read-only) */
#define TIMER_REG_CFG       0x08    /* Configuration */
#define TIMER_REG_INT_STAT  0x0C    /* Interrupt status */
#define TIMER_REG_INT_EN    0x10    /* Interrupt enable */
#define TIMER_REG_CMP0      0x14    /* Compare register 0 */
#define TIMER_REG_CMP1      0x1C    /* Compare register 1 */
#define TIMER_REG_CMP2      0x24    /* Compare register 2 */
#define TIMER_REG_CMP3      0x2C    /* Compare register 3 */
#define TIMER_REG_STAT      0x34    /* Status */

/* ── CFG bits ──────────────────────────────────── */
#define TIMER_CFG_ENABLE    (1 << 0)
#define TIMER_CFG_FREQ_SEL  (1 << 1)  /* 0=24MHz, 1=1us tick */

/* ── INT_STAT bits ─────────────────────────────── */
#define TIMER_INT_CMP0      (1 << 0)
#define TIMER_INT_CMP1      (1 << 1)
#define TIMER_INT_CMP2      (1 << 2)
#define TIMER_INT_CMP3      (1 << 3)

/* ── Default tick rate ─────────────────────────── */
#define TIMER_DEFAULT_HZ    24000000ULL  /* 24 MHz reference */

/* ── State ─────────────────────────────────────── */
typedef struct {
    /* Counter (free-running, monotonically increasing) */
    uint64_t counter;

    /* Configuration */
    uint32_t cfg;
    uint32_t int_en;
    uint32_t int_stat;
    uint32_t stat;

    /* Compare registers */
    uint64_t cmp[4];

    /* Last tick timestamp for rate approximation */
    uint64_t last_update_ns;
} apple_timer_state_t;

/* ── API ───────────────────────────────────────── */
void apple_timer_init(apple_timer_state_t *tmr);
void apple_timer_tick(apple_timer_state_t *tmr, uint64_t now_ns);

uint64_t apple_timer_mmio_read(apple_timer_state_t *tmr, uint64_t offset, int size);
void     apple_timer_mmio_write(apple_timer_state_t *tmr, uint64_t offset,
                                 uint64_t value, int size);

#endif /* SILICONV_APPLE_TIMER_H */
