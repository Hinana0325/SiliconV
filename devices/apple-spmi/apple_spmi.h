/*
 * SiliconV — Apple SPMI (System Power Management Interface)
 *
 * Implements the Apple SPMI controller used in Apple Silicon SoCs.
 * Provides a register-based interface for communicating with the
 * PMIC (Power Management IC) for voltage scaling, power gating,
 * and thermal management.
 */

#ifndef SILICONV_APPLE_SPMI_H
#define SILICONV_APPLE_SPMI_H

#include <stdint.h>
#include <stdbool.h>

/* ── Register Offsets ──────────────────────────── */
#define SPMI_REG_CMD        0x00    /* Command register */
#define SPMI_REG_ADDR       0x04    /* Address register */
#define SPMI_REG_DATA       0x08    /* Data register */
#define SPMI_REG_STAT       0x0C    /* Status */
#define SPMI_REG_CFG        0x10    /* Configuration */
#define SPMI_REG_INT_EN     0x14    /* Interrupt enable */
#define SPMI_REG_INT_STAT   0x18    /* Interrupt status */
#define SPMI_REG_PPID       0x1C    /* Peripheral ID */

/* ── CMD bits ──────────────────────────────────── */
#define SPMI_CMD_EXT_READ    (0x04)  /* Extended register read */
#define SPMI_CMD_EXT_WRITE   (0x05)  /* Extended register write */
#define SPMI_CMD_BYTE_READ   (0x06)  /* Byte read */
#define SPMI_CMD_BYTE_WRITE  (0x07)  /* Byte write */

/* ── STAT bits ─────────────────────────────────── */
#define SPMI_STAT_BUSY      (1 << 0)
#define SPMI_STAT_DONE      (1 << 1)
#define SPMI_STAT_ERROR     (1 << 2)
#define SPMI_STAT_READY     (1 << 3)

/* ── CFG bits ──────────────────────────────────── */
#define SPMI_CFG_ENABLE     (1 << 0)
#define SPMI_CFG_MASTER     (1 << 1)

/* ── State ─────────────────────────────────────── */
typedef struct {
    uint32_t cmd;
    uint32_t addr;
    uint32_t data;
    uint32_t stat;
    uint32_t cfg;
    uint32_t int_en;
    uint32_t int_stat;
    uint32_t ppid;

    /* Simulated PMIC register file (256 bytes) */
    uint8_t pmic_regs[256];
} apple_spmi_state_t;

/* ── API ───────────────────────────────────────── */
void apple_spmi_init(apple_spmi_state_t *spmi);

uint64_t apple_spmi_mmio_read(apple_spmi_state_t *spmi, uint64_t offset, int size);
void     apple_spmi_mmio_write(apple_spmi_state_t *spmi, uint64_t offset,
                                uint64_t value, int size);

#endif /* SILICONV_APPLE_SPMI_H */
