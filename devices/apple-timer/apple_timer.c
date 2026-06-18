/*
 * SiliconV — Apple Timer (Implementation)
 */

#include "apple_timer.h"
#include <string.h>

void apple_timer_init(apple_timer_state_t *tmr)
{
    memset(tmr, 0, sizeof(*tmr));
    tmr->counter = 0;
    tmr->cfg = TIMER_CFG_ENABLE;
    tmr->cmp[0] = ~0ULL;
    tmr->cmp[1] = ~0ULL;
    tmr->cmp[2] = ~0ULL;
    tmr->cmp[3] = ~0ULL;
    tmr->last_update_ns = 0;
}

void apple_timer_tick(apple_timer_state_t *tmr, uint64_t now_ns)
{
    if (!(tmr->cfg & TIMER_CFG_ENABLE))
        return;

    if (tmr->last_update_ns == 0) {
        tmr->last_update_ns = now_ns;
        return;
    }

    uint64_t elapsed_ns = now_ns - tmr->last_update_ns;
    if (elapsed_ns < 1000)
        return; /* Don't update more often than 1us */

    /* Convert elapsed ns to timer ticks */
    uint64_t hz = (tmr->cfg & TIMER_CFG_FREQ_SEL) ? 1000000ULL : TIMER_DEFAULT_HZ;
    uint64_t ticks_per_ns = hz / 1000000000ULL;
    if (ticks_per_ns == 0) ticks_per_ns = 1;

    uint64_t increment = (elapsed_ns * ticks_per_ns) / 1000;
    if (increment == 0) increment = 1;

    tmr->counter += increment;
    tmr->last_update_ns = now_ns;

    /* Check compare registers for match */
    for (int i = 0; i < 4; i++) {
        if (tmr->counter >= tmr->cmp[i] && tmr->cmp[i] != ~0ULL) {
            tmr->int_stat |= (1 << i);
            tmr->cmp[i] = ~0ULL; /* One-shot by default */
        }
    }
}

uint64_t apple_timer_mmio_read(apple_timer_state_t *tmr, uint64_t offset, int size)
{
    (void)size;
    switch (offset) {
    case TIMER_REG_CNT_LOW:
        return (uint32_t)(tmr->counter & 0xFFFFFFFF);
    case TIMER_REG_CNT_HIGH:
        return (uint32_t)(tmr->counter >> 32);
    case TIMER_REG_CFG:
        return tmr->cfg;
    case TIMER_REG_INT_STAT:
        return tmr->int_stat;
    case TIMER_REG_INT_EN:
        return tmr->int_en;
    case TIMER_REG_CMP0:
        return (uint32_t)(tmr->cmp[0] & 0xFFFFFFFF);
    case TIMER_REG_CMP0 + 4:
        return (uint32_t)(tmr->cmp[0] >> 32);
    case TIMER_REG_CMP1:
        return (uint32_t)(tmr->cmp[1] & 0xFFFFFFFF);
    case TIMER_REG_CMP1 + 4:
        return (uint32_t)(tmr->cmp[1] >> 32);
    case TIMER_REG_CMP2:
        return (uint32_t)(tmr->cmp[2] & 0xFFFFFFFF);
    case TIMER_REG_CMP2 + 4:
        return (uint32_t)(tmr->cmp[2] >> 32);
    case TIMER_REG_CMP3:
        return (uint32_t)(tmr->cmp[3] & 0xFFFFFFFF);
    case TIMER_REG_CMP3 + 4:
        return (uint32_t)(tmr->cmp[3] >> 32);
    case TIMER_REG_STAT:
        return tmr->stat;
    default:
        return 0;
    }
}

void apple_timer_mmio_write(apple_timer_state_t *tmr, uint64_t offset,
                             uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)value;

    switch (offset) {
    case TIMER_REG_CFG:
        tmr->cfg = v & (TIMER_CFG_ENABLE | TIMER_CFG_FREQ_SEL);
        break;
    case TIMER_REG_INT_STAT:
        tmr->int_stat &= ~v; /* Write 1 to clear */
        break;
    case TIMER_REG_INT_EN:
        tmr->int_en = v & 0x0F;
        break;
    case TIMER_REG_CMP0:
        tmr->cmp[0] = (tmr->cmp[0] & 0xFFFFFFFF00000000ULL) | v;
        break;
    case TIMER_REG_CMP0 + 4:
        tmr->cmp[0] = (tmr->cmp[0] & 0xFFFFFFFFULL) | ((uint64_t)v << 32);
        break;
    case TIMER_REG_CMP1:
        tmr->cmp[1] = (tmr->cmp[1] & 0xFFFFFFFF00000000ULL) | v;
        break;
    case TIMER_REG_CMP1 + 4:
        tmr->cmp[1] = (tmr->cmp[1] & 0xFFFFFFFFULL) | ((uint64_t)v << 32);
        break;
    case TIMER_REG_CMP2:
        tmr->cmp[2] = (tmr->cmp[2] & 0xFFFFFFFF00000000ULL) | v;
        break;
    case TIMER_REG_CMP2 + 4:
        tmr->cmp[2] = (tmr->cmp[2] & 0xFFFFFFFFULL) | ((uint64_t)v << 32);
        break;
    case TIMER_REG_CMP3:
        tmr->cmp[3] = (tmr->cmp[3] & 0xFFFFFFFF00000000ULL) | v;
        break;
    case TIMER_REG_CMP3 + 4:
        tmr->cmp[3] = (tmr->cmp[3] & 0xFFFFFFFFULL) | ((uint64_t)v << 32);
        break;
    }
}
