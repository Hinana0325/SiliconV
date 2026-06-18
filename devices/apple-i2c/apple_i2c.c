/*
 * SiliconV — Apple I2C Bus Controller (Implementation)
 */

#include "apple_i2c.h"
#include <string.h>

void apple_i2c_init(apple_i2c_state_t *i2c)
{
    memset(i2c, 0, sizeof(*i2c));
    i2c->clk_div = 0x1F; /* Default divider */
    i2c->stat = I2C_STAT_BUS_IDLE;
}

uint64_t apple_i2c_mmio_read(apple_i2c_state_t *i2c, uint64_t offset, int size)
{
    (void)size;
    switch (offset) {
    case I2C_REG_CTRL:     return i2c->ctrl;
    case I2C_REG_STAT:     return i2c->stat;
    case I2C_REG_ADDR:     return i2c->addr;
    case I2C_REG_DATA:     return i2c->data;
    case I2C_REG_CLK_DIV:  return i2c->clk_div;
    case I2C_REG_INT_EN:   return i2c->int_en;
    case I2C_REG_INT_STAT: return i2c->int_stat;
    default:               return 0;
    }
}

void apple_i2c_mmio_write(apple_i2c_state_t *i2c, uint64_t offset,
                           uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)value;

    switch (offset) {
    case I2C_REG_CTRL:
        i2c->ctrl = v & (I2C_CTRL_ENABLE | I2C_CTRL_START |
                          I2C_CTRL_STOP | I2C_CTRL_READ |
                          I2C_CTRL_WRITE | I2C_CTRL_ACK);
        if (v & I2C_CTRL_START) {
            i2c->stat &= ~I2C_STAT_BUS_IDLE;
            i2c->stat |= I2C_STAT_BUSY;
        }
        if (v & I2C_CTRL_STOP) {
            i2c->stat &= ~I2C_STAT_BUSY;
            i2c->stat |= (I2C_STAT_BUS_IDLE | I2C_STAT_TX_DONE);
        }
        if (v & I2C_CTRL_WRITE) {
            /* Simulate write completion */
            i2c->stat |= I2C_STAT_TX_DONE;
        }
        if (v & I2C_CTRL_READ) {
            /* Return dummy data for reads */
            i2c->data = 0xFF;
            i2c->stat |= I2C_STAT_RX_RDY;
        }
        break;
    case I2C_REG_ADDR:
        i2c->addr = v & 0x7F;
        break;
    case I2C_REG_DATA:
        i2c->data = v;
        break;
    case I2C_REG_CLK_DIV:
        i2c->clk_div = v;
        break;
    case I2C_REG_INT_EN:
        i2c->int_en = v & 0x3F;
        break;
    case I2C_REG_INT_STAT:
        i2c->int_stat &= ~v; /* Write 1 to clear */
        break;
    }
}
