/*
 * SiliconV — Apple I2C Bus Controller
 *
 * Implements the Apple I2C controller used in Apple Silicon SoCs.
 * Provides a simple serial bus for communicating with PMIC, sensors,
 * and other low-speed peripherals.
 */

#ifndef SILICONV_APPLE_I2C_H
#define SILICONV_APPLE_I2C_H

#include <stdint.h>
#include <stdbool.h>

/* ── Register Offsets ──────────────────────────── */
#define I2C_REG_CTRL        0x00    /* Control */
#define I2C_REG_STAT        0x04    /* Status */
#define I2C_REG_ADDR        0x08    /* Slave address */
#define I2C_REG_DATA        0x0C    /* Data register */
#define I2C_REG_CLK_DIV     0x10    /* Clock divider */
#define I2C_REG_INT_EN      0x14    /* Interrupt enable */
#define I2C_REG_INT_STAT    0x18    /* Interrupt status */

/* ── CTRL bits ─────────────────────────────────── */
#define I2C_CTRL_ENABLE     (1 << 0)
#define I2C_CTRL_START      (1 << 1)  /* Send start condition */
#define I2C_CTRL_STOP       (1 << 2)  /* Send stop condition */
#define I2C_CTRL_READ       (1 << 3)  /* Read mode */
#define I2C_CTRL_WRITE      (1 << 4)  /* Write mode */
#define I2C_CTRL_ACK        (1 << 5)  /* Acknowledge enable */

/* ── STAT bits ─────────────────────────────────── */
#define I2C_STAT_BUSY       (1 << 0)  /* Bus busy */
#define I2C_STAT_TX_DONE    (1 << 1)  /* Transmission complete */
#define I2C_STAT_RX_RDY     (1 << 2)  /* Receive data ready */
#define I2C_STAT_NACK       (1 << 3)  /* No acknowledge */
#define I2C_STAT_ARB_LOST   (1 << 4)  /* Arbitration lost */
#define I2C_STAT_BUS_IDLE   (1 << 5)  /* Bus idle */

/* ── State ─────────────────────────────────────── */
typedef struct {
    uint32_t ctrl;
    uint32_t stat;
    uint32_t addr;
    uint32_t data;
    uint32_t clk_div;
    uint32_t int_en;
    uint32_t int_stat;
} apple_i2c_state_t;

/* ── API ───────────────────────────────────────── */
void apple_i2c_init(apple_i2c_state_t *i2c);

uint64_t apple_i2c_mmio_read(apple_i2c_state_t *i2c, uint64_t offset, int size);
void     apple_i2c_mmio_write(apple_i2c_state_t *i2c, uint64_t offset,
                               uint64_t value, int size);

#endif /* SILICONV_APPLE_I2C_H */
