/*
 * SiliconV — TCG Backend (Public Interface)
 *
 * Pure-software ARM64 emulation backend for x86_64 hosts.
 * Implements sv_hv_ops_t for transparent drop-in replacement of KVM/HVF.
 */

#ifndef SILICONV_TCG_H
#define SILICONV_TCG_H

#include "../abstraction/hv.h"
#include <stdbool.h>
#include <stdint.h>

/* ── ARM64 Register Indices (matches KVM register IDs) ──── */
#define TCG_ARM64_CORE_REG(x)     (x)           /* x0-x30 */
#define TCG_ARM64_CORE_REG_SP     31
#define TCG_ARM64_CORE_REG_PC     32
#define TCG_ARM64_CORE_REG_PSTATE 33

/* System register encoding: Op0:Op1:CRn:CRm:Op2 packed into uint64 */
#define TCG_SYSREG(op0, op1, crn, crm, op2) \
    (((uint64_t)(op0) << 36) | ((uint64_t)(op1) << 32) | \
     ((uint64_t)(crn) << 28) | ((uint64_t)(crm) << 24) | \
     ((uint64_t)(op2) << 20))

/* ── PSTATE flags ─────────────────────────────────────── */
#define PSTATE_N  (1U << 31)
#define PSTATE_Z  (1U << 30)
#define PSTATE_C  (1U << 29)
#define PSTATE_V  (1U << 28)
#define PSTATE_D  (1U << 9)   /* Debug mask */
#define PSTATE_A  (1U << 8)   /* SError mask */
#define PSTATE_I  (1U << 7)   /* IRQ mask */
#define PSTATE_F  (1U << 6)   /* FIQ mask */
#define PSTATE_EL_MASK  0x3   /* Exception level (EL0=0, EL1=1, etc.) */

/* ── TCG VM ────────────────────────────────────────────── */
typedef struct tcg_vm {
    sv_vm_config_t config;

    uint8_t  *ram;          /* Guest RAM */
    uint64_t  ram_size;

    /* MMIO callback */
    uint64_t (*mmio_read)(void *opaque, uint64_t addr, int size);
    void     (*mmio_write)(void *opaque, uint64_t addr, uint64_t value, int size);
    void      *callback_opaque;

    int       num_vcpus;
} tcg_vm_t;

/* ── TCG vCPU ──────────────────────────────────────────── */
typedef struct tcg_vcpu {
    int       id;
    tcg_vm_t *vm;

    /* General-purpose registers */
    uint64_t  x[31];

    /* Special registers */
    uint64_t  pc;
    uint64_t  sp_el0;
    uint64_t  sp_el1;
    uint64_t  elr_el1;
    uint64_t  spsr_el1;

    /* MMU registers */
    uint64_t  ttbr0_el1;
    uint64_t  ttbr1_el1;
    uint64_t  tcr_el1;
    uint64_t  mair_el1;
    uint64_t  sctlr_el1;     /* MMU on/off */
    uint64_t  vbar_el1;      /* Vector base */

    /* Exception/fault */
    uint64_t  far_el1;
    uint64_t  esr_el1;

    /* Processor state */
    uint32_t  pstate;        /* NZCV + DAIF + EL */

    /* Timers */
    uint64_t  cntfrq_el0;    /* Counter frequency */
    uint64_t  cntvct_el0;    /* Virtual counter value */
    uint64_t  cntv_ctl_el0;  /* Timer control */
    uint64_t  cntv_cval_el0; /* Timer compare value */
    bool      timer_irq_pending;

    /* Running state */
    bool      running;

    /* Exit info (populated on MMIO/HVC/etc) */
    sv_vcpu_exit_t exit;

    /* Statistics */
    uint64_t  insn_count;
    uint64_t  exit_count;
    uint64_t  mmio_exits;
} tcg_vcpu_t;

/* ── Public API ────────────────────────────────────────── */

/* Get the TCG backend ops */
const sv_hv_ops_t* tcg_get_ops(void);

/* Initialize TCG backend */
int tcg_init(void);

/* Create a TCG VM */
sv_vm_t* tcg_vm_create(const sv_vm_config_t *config);

/* Destroy a TCG VM */
void tcg_vm_destroy(sv_vm_t *vm);

/* Register an MMIO region (informational, MMIO dispatch via callbacks) */
int tcg_mmio_register(sv_vm_t *vm, uint64_t addr, uint64_t size,
                      const sv_mmio_handler_t *handler);

/* Load kernel/DTB into guest RAM */
int tcg_load_kernel(sv_vm_t *vm, const char *path);
int tcg_load_dtb(sv_vm_t *vm, const char *path);

/* Create a vCPU */
sv_vcpu_t* tcg_vcpu_create(sv_vm_t *vm, int id);

/* Run a vCPU */
int tcg_vcpu_run(sv_vcpu_t *vcpu, sv_vcpu_exit_t *exit);

/* Get/set vCPU register */
int tcg_vcpu_get_reg(sv_vcpu_t *vcpu, uint64_t reg, uint64_t *val);
int tcg_vcpu_set_reg(sv_vcpu_t *vcpu, uint64_t reg, uint64_t val);

/* Shutdown TCG backend */
void tcg_shutdown(void);

#endif /* SILICONV_TCG_H */
