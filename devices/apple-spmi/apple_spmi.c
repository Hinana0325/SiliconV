/*
 * SiliconV — Apple SPMI (System Power Management Interface) — Implementation
 */

#include "apple_spmi.h"
#include <string.h>

void apple_spmi_init(apple_spmi_state_t *spmi)
{
    memset(spmi, 0, sizeof(*spmi));
    spmi->cfg = SPMI_CFG_ENABLE | SPMI_CFG_MASTER;
    spmi->stat = SPMI_STAT_READY;
    spmi->ppid = 0x53494C01; /* "SIL" + version */

    /* Initialize simulated PMIC register file with sensible defaults */
    spmi->pmic_regs[0x00] = 0x80; /* Device ID (PMU vendor) */
    spmi->pmic_regs[0x01] = 0x01; /* Device revision */
    spmi->pmic_regs[0x10] = 0x00; /* PMIC status: OK */
    spmi->pmic_regs[0x20] = 0xFF; /* All power rails enabled */
    spmi->pmic_regs[0x30] = 0x0F; /* Thermal zone status: normal */
}

uint64_t apple_spmi_mmio_read(apple_spmi_state_t *spmi, uint64_t offset, int size)
{
    (void)size;
    switch (offset) {
    case SPMI_REG_CMD:     return spmi->cmd;
    case SPMI_REG_ADDR:    return spmi->addr;
    case SPMI_REG_DATA:    return spmi->data;
    case SPMI_REG_STAT:    return spmi->stat;
    case SPMI_REG_CFG:     return spmi->cfg;
    case SPMI_REG_INT_EN:  return spmi->int_en;
    case SPMI_REG_INT_STAT: return spmi->int_stat;
    case SPMI_REG_PPID:    return spmi->ppid;
    default:               return 0;
    }
}

void apple_spmi_mmio_write(apple_spmi_state_t *spmi, uint64_t offset,
                            uint64_t value, int size)
{
    (void)size;
    uint32_t v = (uint32_t)value;

    switch (offset) {
    case SPMI_REG_CMD:
        spmi->cmd = v;
        spmi->stat &= ~SPMI_STAT_READY;
        spmi->stat |= SPMI_STAT_BUSY;

        /* Execute SPMI command on simulated PMIC register file */
        if ((v == SPMI_CMD_EXT_READ || v == SPMI_CMD_BYTE_READ) &&
            spmi->addr < sizeof(spmi->pmic_regs)) {
            spmi->data = spmi->pmic_regs[spmi->addr];
            spmi->stat |= SPMI_STAT_DONE;
        } else if ((v == SPMI_CMD_EXT_WRITE || v == SPMI_CMD_BYTE_WRITE) &&
                   spmi->addr < sizeof(spmi->pmic_regs)) {
            spmi->pmic_regs[spmi->addr] = (uint8_t)spmi->data;
            spmi->stat |= SPMI_STAT_DONE;
        } else if (v == 0) {
            /* Idle command */
            spmi->stat |= SPMI_STAT_READY;
        } else {
            spmi->stat |= SPMI_STAT_ERROR;
        }

        spmi->stat &= ~SPMI_STAT_BUSY;
        break;
    case SPMI_REG_ADDR:
        spmi->addr = v;
        break;
    case SPMI_REG_DATA:
        spmi->data = v;
        break;
    case SPMI_REG_CFG:
        spmi->cfg = v & (SPMI_CFG_ENABLE | SPMI_CFG_MASTER);
        break;
    case SPMI_REG_INT_EN:
        spmi->int_en = v & 0x0F;
        break;
    case SPMI_REG_INT_STAT:
        spmi->int_stat &= ~v; /* Write 1 to clear */
        break;
    }
}
