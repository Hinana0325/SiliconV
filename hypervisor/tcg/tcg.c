/*
 * SiliconV — TCG Backend (Entry Point)
 *
 * Implements sv_hv_ops_t for pure-software ARM64 emulation.
 * Designed as a drop-in replacement for KVM/HVF backends on x86_64 hosts.
 *
 * Architecture:
 *   ARM64 EL1 kernel → decode → execute → MMIO exit → machine dispatch → resume
 *
 * Execution model (3 tiers):
 *   Tier 0: Switch-based interpreter (all instructions, always works)
 *   Tier 1: Inline-threaded interpreter (planned)
 *   Tier 2: Hot-path JIT → x86_64 native code (planned)
 */

#include "tcg.h"
#include "tcg_cpu.h"
#include "tcg_decode.h"
#include "tcg_exec.h"
#include "tcg_mmu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Static state ──────────────────────────────────────── */
static bool tcg_initialized = false;

/* ── VM allocation ─────────────────────────────────────── */
sv_vm_t* tcg_vm_create(const sv_vm_config_t *config)
{
    if (!config) return NULL;

    tcg_vm_t *vm = calloc(1, sizeof(tcg_vm_t));
    if (!vm) return NULL;

    vm->config = *config;

    /* Allocate guest RAM */
    if (config->preallocated_ram) {
        /* Use pre-loaded RAM from machine.c (owns the memory) */
        vm->ram = config->preallocated_ram;
        vm->ram_size = config->ram_size;
    } else {
        vm->ram = calloc(1, config->ram_size);
        if (!vm->ram) { free(vm); return NULL; }
        vm->ram_size = config->ram_size;
    }

    vm->mmio_read  = config->mmio_read;
    vm->mmio_write = config->mmio_write;
    vm->callback_opaque = config->callback_opaque;
    vm->num_vcpus = config->num_cpus;

    printf("tcg: VM created (%d vCPUs, %lu MB RAM)\n",
           vm->num_vcpus, (unsigned long)(vm->ram_size / (1024 * 1024)));

    return (sv_vm_t *)vm;
}

void tcg_vm_destroy(sv_vm_t *vm_)
{
    tcg_vm_t *vm = (tcg_vm_t *)vm_;
    if (!vm) return;

    /* Only free RAM if we allocated it (not preallocated from machine.c) */
    if (vm->ram && !vm->config.preallocated_ram) {
        free(vm->ram);
    }
    free(vm);
    printf("tcg: VM destroyed\n");
}

/* ── MMIO Region Registration ──────────────────────────── */
int tcg_mmio_register(sv_vm_t *vm_, uint64_t addr, uint64_t size,
                      const sv_mmio_handler_t *handler)
{
    (void)vm_;
    (void)addr;
    (void)size;
    (void)handler;
    /* MMIO dispatch is handled via callbacks set during VM creation.
     * This function exists for KVM compatibility (KVM needs to register
     * KVM_CAP_ARM_DEVICE regions). TCG backend uses direct address-range
     * checking in tcg_mmu_resolve(), no registration needed. */
    return 0;
}

/* ── Kernel/DTB Loading ────────────────────────────────── */
int tcg_load_kernel(sv_vm_t *vm_, const char *path)
{
    (void)vm_;
    (void)path;
    /* Kernel is pre-loaded into guest RAM by machine.c.
     * The machine module handles kernel/DTB/initrd loading.
     * TCG backend starts execution at vm->config.kernel_entry. */
    return 0;
}

int tcg_load_dtb(sv_vm_t *vm_, const char *path)
{
    (void)vm_;
    (void)path;
    /* DTB is also pre-loaded by machine.c */
    return 0;
}

/* ── vCPU operations ───────────────────────────────────── */
sv_vcpu_t* tcg_vcpu_create(sv_vm_t *vm_, int id)
{
    tcg_vm_t *vm = (tcg_vm_t *)vm_;
    if (!vm || id < 0 || id >= vm->num_vcpus) return NULL;

    tcg_vcpu_t *vcpu = calloc(1, sizeof(tcg_vcpu_t));
    if (!vcpu) return NULL;

    vcpu->id = id;
    vcpu->vm = vm;

    /* Default EL1 state (kernel starts at EL1) */
    vcpu->pstate = PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F | 0x1; /* EL1h, all masked */
    vcpu->cntfrq_el0 = 62500000; /* 62.5 MHz (QEMU virt default) */
    vcpu->running = false;

    printf("tcg: vCPU %d created\n", id);
    return (sv_vcpu_t *)vcpu;
}

int tcg_vcpu_get_reg(sv_vcpu_t *vcpu_, uint64_t reg, uint64_t *val)
{
    tcg_vcpu_t *vcpu = (tcg_vcpu_t *)vcpu_;
    if (!vcpu || !val) return -1;

    switch (reg) {
    case TCG_ARM64_CORE_REG_PC:      *val = vcpu->pc; break;
    case TCG_ARM64_CORE_REG_SP:      *val = vcpu->sp_el1; break;
    case TCG_ARM64_CORE_REG_PSTATE:  *val = vcpu->pstate; break;
    default:
        if (reg < 31) *val = vcpu->x[reg];
        else return -1;
    }
    return 0;
}

int tcg_vcpu_set_reg(sv_vcpu_t *vcpu_, uint64_t reg, uint64_t val)
{
    tcg_vcpu_t *vcpu = (tcg_vcpu_t *)vcpu_;
    if (!vcpu) return -1;

    switch (reg) {
    case TCG_ARM64_CORE_REG_PC:      vcpu->pc = val; break;
    case TCG_ARM64_CORE_REG_SP:      vcpu->sp_el1 = val; break;
    case TCG_ARM64_CORE_REG_PSTATE:  vcpu->pstate = (uint32_t)val; break;
    default:
        if (reg < 31) vcpu->x[reg] = val;
        else return -1;
    }
    return 0;
}

/* ── Initialization / Shutdown ─────────────────────────── */
int tcg_init(void)
{
    if (tcg_initialized) return 0;

    printf("tcg: ARM64 software emulation backend initialized\n");
    printf("tcg: Host: x86_64 | Guest: ARM64 (AArch64)\n");
    printf("tcg: Execution: interpreted (switch-dispatch)\n");

    tcg_initialized = true;
    return 0;
}

void tcg_shutdown(void)
{
    if (!tcg_initialized) return;
    printf("tcg: backend shutdown\n");
    tcg_initialized = false;
}

/* ── Backend ops (sv_hv_ops_t) ─────────────────────────── */
static const sv_hv_ops_t tcg_ops = {
    .name         = "TCG (ARM64→x86_64 software emulation)",
    .type         = SV_HV_TCG,
    .init         = tcg_init,
    .vm_create    = tcg_vm_create,
    .vm_destroy   = tcg_vm_destroy,
    .mmio_register = tcg_mmio_register,
    .load_kernel  = tcg_load_kernel,
    .load_dtb     = tcg_load_dtb,
    .vcpu_create  = tcg_vcpu_create,
    .vcpu_run     = tcg_vcpu_run,
    .vcpu_get_reg = tcg_vcpu_get_reg,
    .vcpu_set_reg = tcg_vcpu_set_reg,
    .shutdown     = tcg_shutdown,
};

const sv_hv_ops_t* tcg_get_ops(void)
{
    return &tcg_ops;
}

/* Auto-register TCG as fallback backend */
__attribute__((constructor))
static void register_tcg(void)
{
    sv_hv_register(&tcg_ops);
}
