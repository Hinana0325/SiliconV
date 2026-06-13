/*
 * SiliconV — TCG CPU State (Internal Header)
 */
#ifndef SILICONV_TCG_CPU_H
#define SILICONV_TCG_CPU_H

/* Re-export: tcg_vcpu_t is defined in tcg.h */
/* Helper macros */
static inline uint64_t tcg_read_reg(tcg_vcpu_t *vcpu, int reg) {
    return (reg < 31) ? vcpu->x[reg] : vcpu->sp_el1;
}

static inline void tcg_write_reg(tcg_vcpu_t *vcpu, int reg, uint64_t val) {
    if (reg < 31) vcpu->x[reg] = val;
}

static inline uint64_t tcg_pc(tcg_vcpu_t *vcpu) {
    return vcpu->pc;
}

#endif /* SILICONV_TCG_CPU_H */
