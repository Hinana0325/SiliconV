/*
 * SiliconV — Apple GPIO (Implementation)
 */

#include "apple_gpio.h"
#include <string.h>

void apple_gpio_init(apple_gpio_state_t *gpio)
{
    memset(gpio, 0, sizeof(*gpio));
    gpio->locked = false;
}

void apple_gpio_set_pin(apple_gpio_state_t *gpio, int pin, bool value)
{
    if (pin < 0 || pin >= GPIO_NUM_PINS)
        return;
    if (!(gpio->cfg[pin] & GPIO_CFG_OUTPUT))
        return; /* Not an output pin */

    gpio->data[pin] = value ? 1 : 0;
}

bool apple_gpio_get_pin(const apple_gpio_state_t *gpio, int pin)
{
    if (pin < 0 || pin >= GPIO_NUM_PINS)
        return false;
    if (!(gpio->cfg[pin] & GPIO_CFG_INPUT))
        return false; /* Not an input pin */

    return gpio->data[pin] != 0;
}

uint64_t apple_gpio_mmio_read(apple_gpio_state_t *gpio, uint64_t offset, int size)
{
    (void)size;

    /* Per-pin configuration / data */
    if (offset < 0x800) {
        int pin = (int)(offset / 8);
        int reg = (int)(offset % 8);
        if (pin < GPIO_NUM_PINS) {
            if (reg == 0) return gpio->cfg[pin];
            if (reg == 4) return gpio->data[pin];
        }
        return 0;
    }

    switch (offset) {
    case GPIO_REG_INT_STAT:  return gpio->int_stat;
    case GPIO_REG_INT_EN:    return gpio->int_en;
    case GPIO_REG_INT_TYP:   return gpio->int_typ;
    case GPIO_REG_INT_POL:   return gpio->int_pol;
    case GPIO_REG_OUT_SET:   return gpio->out_shadow;
    case GPIO_REG_OUT_CLR:   return gpio->out_shadow;
    case GPIO_REG_LOCK:      return gpio->locked ? 1 : 0;
    default:                 return 0;
    }
}

void apple_gpio_mmio_write(apple_gpio_state_t *gpio, uint64_t offset,
                            uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)value;

    /* Per-pin configuration / data */
    if (offset < 0x800) {
        int pin = (int)(offset / 8);
        int reg = (int)(offset % 8);
        if (pin < GPIO_NUM_PINS) {
            if (reg == 0) gpio->cfg[pin] = v & 0x1F;
            if (reg == 4 && (gpio->cfg[pin] & GPIO_CFG_OUTPUT))
                gpio->data[pin] = v & 1;
        }
        return;
    }

    switch (offset) {
    case GPIO_REG_INT_STAT:
        gpio->int_stat &= ~v; /* Write 1 to clear */
        break;
    case GPIO_REG_INT_EN:
        gpio->int_en = v;
        break;
    case GPIO_REG_INT_TYP:
        gpio->int_typ = v;
        break;
    case GPIO_REG_INT_POL:
        gpio->int_pol = v;
        break;
    case GPIO_REG_OUT_SET:
        gpio->out_shadow |= v;
        break;
    case GPIO_REG_OUT_CLR:
        gpio->out_shadow &= ~v;
        break;
    case GPIO_REG_LOCK:
        gpio->locked = true;
        break;
    }
}
