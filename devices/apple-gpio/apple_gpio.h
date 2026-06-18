/*
 * SiliconV — Apple GPIO
 *
 * Implements the Apple GPIO controller used in Apple Silicon SoCs.
 * Manages up to 256 GPIO pins with configurable direction, pull,
 * and interrupt settings. Used by iBoot and XNU for board-level
 * control (power rails, reset lines, etc.).
 */

#ifndef SILICONV_APPLE_GPIO_H
#define SILICONV_APPLE_GPIO_H

#include <stdint.h>
#include <stdbool.h>

/* ── Number of pins ────────────────────────────── */
#define GPIO_NUM_PINS       256

/* ── Register Offsets (per-pin stride = 8 bytes) ─ */
#define GPIO_REG_CFG(pin)   (0x000 + (pin) * 8)   /* Configuration */
#define GPIO_REG_DATA(pin)  (0x004 + (pin) * 8)   /* Data value */

#define GPIO_REG_INT_STAT   0x800   /* Interrupt status */
#define GPIO_REG_INT_EN     0x804   /* Interrupt enable */
#define GPIO_REG_INT_TYP    0x808   /* Interrupt type (edge/level) */
#define GPIO_REG_INT_POL    0x80C   /* Interrupt polarity */
#define GPIO_REG_OUT_SET    0x810   /* Output set */
#define GPIO_REG_OUT_CLR    0x814   /* Output clear */
#define GPIO_REG_LOCK       0x818   /* Lock register */

/* ── CFG bits ──────────────────────────────────── */
#define GPIO_CFG_OUTPUT     (1 << 0)
#define GPIO_CFG_INPUT      (1 << 1)
#define GPIO_CFG_PULL_UP    (1 << 2)
#define GPIO_CFG_PULL_DOWN  (1 << 3)
#define GPIO_CFG_OPEN_DRAIN (1 << 4)

/* ── INT_TYP bits ──────────────────────────────── */
#define GPIO_INT_EDGE       (0 << 0)
#define GPIO_INT_LEVEL      (1 << 0)

/* ── INT_POL bits ──────────────────────────────── */
#define GPIO_POL_HIGH       (0 << 0)
#define GPIO_POL_LOW        (1 << 0)

/* ── State ─────────────────────────────────────── */
typedef struct {
    /* Per-pin configuration and data */
    uint32_t cfg[GPIO_NUM_PINS];
    uint32_t data[GPIO_NUM_PINS];

    /* Interrupt state */
    uint32_t int_stat;
    uint32_t int_en;
    uint32_t int_typ;
    uint32_t int_pol;

    /* Output set/clear shadow */
    uint32_t out_shadow;

    bool locked;
} apple_gpio_state_t;

/* ── API ───────────────────────────────────────── */
void apple_gpio_init(apple_gpio_state_t *gpio);

void apple_gpio_set_pin(apple_gpio_state_t *gpio, int pin, bool value);
bool apple_gpio_get_pin(const apple_gpio_state_t *gpio, int pin);

uint64_t apple_gpio_mmio_read(apple_gpio_state_t *gpio, uint64_t offset, int size);
void     apple_gpio_mmio_write(apple_gpio_state_t *gpio, uint64_t offset,
                                uint64_t value, int size);

#endif /* SILICONV_APPLE_GPIO_H */
