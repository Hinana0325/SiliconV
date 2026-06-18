/*
 * SiliconV — Apple Watchdog Timer (Implementation)
 */

#include "apple_wdt.h"
#include <string.h>

void apple_wdt_init(apple_wdt_state_t *wdt)
{
    memset(wdt, 0, sizeof(*wdt));
    wdt->load_value = 0xFFFFFFFF;
    wdt->current = 0xFFFFFFFF;
    wdt->locked = true;
    wdt->last_tick_ns = 0;
}

void apple_wdt_tick(apple_wdt_state_t *wdt, uint64_t now_ns)
{
    if (!(wdt->ctrl & WDT_CTRL_ENABLE))
        return;

    if (wdt->last_tick_ns == 0) {
        wdt->last_tick_ns = now_ns;
        return;
    }

    uint64_t elapsed = now_ns - wdt->last_tick_ns;
    if (elapsed >= 1000000) { /* 1ms granularity */
        uint32_t decrement = (uint32_t)(elapsed / 1000000);
        if (decrement >= wdt->current) {
            wdt->current = 0;
            wdt->stat |= WDT_STAT_TIMEOUT;
            /* Timeout — would trigger system reset */
        } else {
            wdt->current -= decrement;
        }
        wdt->last_tick_ns = now_ns;
    }
}

uint64_t apple_wdt_mmio_read(apple_wdt_state_t *wdt, uint64_t offset, int size)
{
    (void)size;
    switch (offset) {
    case WDT_REG_CTRL:    return wdt->ctrl;
    case WDT_REG_LOAD:    return wdt->load_value;
    case WDT_REG_CURRENT: return wdt->current;
    case WDT_REG_STAT:    return wdt->stat;
    case WDT_REG_INT_EN:  return wdt->int_en;
    case WDT_REG_LOCK:    return wdt->locked ? 1 : 0;
    default:              return 0;
    }
}

void apple_wdt_mmio_write(apple_wdt_state_t *wdt, uint64_t offset,
                           uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)value;

    switch (offset) {
    case WDT_REG_CTRL:
        wdt->ctrl = v & (WDT_CTRL_ENABLE | WDT_CTRL_AUTO_RST);
        if (v & WDT_CTRL_ENABLE)
            wdt->current = wdt->load_value;
        break;
    case WDT_REG_LOAD:
        wdt->load_value = v;
        break;
    case WDT_REG_STAT:
        wdt->stat &= ~v; /* Clear bits by writing 1 */
        break;
    case WDT_REG_INT_EN:
        wdt->int_en = v & 1;
        break;
    case WDT_REG_UNLOCK:
        if (v == WDT_UNLOCK_MAGIC)
            wdt->locked = false;
        break;
    case WDT_REG_LOCK:
        wdt->locked = true;
        break;
    }
}
