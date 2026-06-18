/*
 * SiliconV — Apple Watchdog Timer
 *
 * Implements the Apple watchdog timer used in Apple Silicon SoCs.
 * Used by iBoot and XNU for system watchdog functionality.
 */

#ifndef SILICONV_APPLE_WDT_H
#define SILICONV_APPLE_WDT_H

#include <stdint.h>
#include <stdbool.h>

/* ── Register Offsets ──────────────────────────── */
#define WDT_REG_CTRL        0x00    /* Control */
#define WDT_REG_LOAD        0x04    /* Load value */
#define WDT_REG_CURRENT     0x08    /* Current counter */
#define WDT_REG_STAT        0x0C    /* Status */
#define WDT_REG_INT_EN      0x14    /* Interrupt enable */
#define WDT_REG_LOCK        0x1C    /* Lock register */
#define WDT_REG_UNLOCK      0x20    /* Unlock sequence */

/* ── CTRL bits ─────────────────────────────────── */
#define WDT_CTRL_ENABLE     (1 << 0)
#define WDT_CTRL_AUTO_RST   (1 << 1)  /* Auto-reset on timeout */

/* ── STAT bits ─────────────────────────────────── */
#define WDT_STAT_TIMEOUT    (1 << 0)  /* Timeout occurred */

/* ── Lock/Unlock values ────────────────────────── */
#define WDT_UNLOCK_MAGIC    0xABCDEF01

/* ── State ─────────────────────────────────────── */
typedef struct {
    uint32_t ctrl;
    uint32_t load_value;
    uint32_t current;
    uint32_t stat;
    uint32_t int_en;
    bool     locked;

    /* For periodic decrement simulation */
    uint64_t last_tick_ns;
} apple_wdt_state_t;

/* ── API ───────────────────────────────────────── */
void apple_wdt_init(apple_wdt_state_t *wdt);
void apple_wdt_tick(apple_wdt_state_t *wdt, uint64_t now_ns);

uint64_t apple_wdt_mmio_read(apple_wdt_state_t *wdt, uint64_t offset, int size);
void     apple_wdt_mmio_write(apple_wdt_state_t *wdt, uint64_t offset,
                               uint64_t value, int size);

#endif /* SILICONV_APPLE_WDT_H */
