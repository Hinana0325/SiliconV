/*
 * SiliconV — HVF Backend (macOS ARM64)
 *
 * Implements the hypervisor abstraction using macOS Hypervisor.framework.
 * Requires: macOS 11.0+, Apple Silicon (ARM64)
 *
 * Conventions:
 * - GICv3 handled by HVF framework (not software emulation)
 * - MMIO for UART/virtio trapped via stage-2 data aborts
 * - vCPU0 boots kernel directly (no boot stub)
 */

#include "../abstraction/hv.h"

#if defined(__arm64__) && defined(__APPLE__)

#include <Hypervisor/Hypervisor.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 16384
#endif

/* ── ESR_ELx decode constants ─────────────────── */
#define ESR_EC_SHIFT  26
#define ESR_EC_MASK   0x3F
#define ESR_EC_DATA_ABORT_EL0 0x20
#define ESR_EC_DATA_ABORT_EL1 0x24
#define ESR_EC_DATA_ABORT_EL2 0x25

#define ISS_SIZE_SHIFT 22
#define ISS_SIZE_MASK  0x3
#define ISS_WNR_BIT    6
#define ISS_DFSC_MASK  0x3F

#define HV_SYS_REG_MPIDR_EL1 0x0005
#define HV_SYS_REG_SCTLR_EL1 0x1000
#define HV_SYS_REG_SP_EL1    0x1C01

/* ── Max resource limits ───────────────────────── */
#define MAX_MMIO_REGIONS 16
#define MAX_VCPUS 8

/* ── MMIO region entry ─────────────────────────── */
typedef struct {
    uint64_t addr;
    uint64_t size;
    sv_mmio_handler_t handler;
} hv_mmio_region_t;

/* ── Internal VM structure ─────────────────────── */
struct sv_vm {
    uint8_t       *ram;
    uint64_t       ram_size;
    uint64_t       ram_base;
    hv_mmio_region_t mmio_regions[MAX_MMIO_REGIONS];
    int            num_mmio_regions;
    int            num_cpus;
    bool           gic_created;
};

/* ── Internal vCPU structure ───────────────────── */
struct sv_vcpu {
    hv_vcpu_t      id;
    hv_vcpu_exit_t *exit;
    int            vcpuid;
    sv_vm_t       *vm;
    pthread_t      thread;
};

/* ── MMIO entry lookup ─────────────────────────── */
static hv_mmio_region_t* hvf_find_mmio(sv_vm_t *vm, uint64_t addr)
{
    for (int i = 0; i < vm->num_mmio_regions; i++) {
        uint64_t start = vm->mmio_regions[i].addr;
        uint64_t end = start + vm->mmio_regions[i].size;
        if (addr >= start && addr < end)
            return &vm->mmio_regions[i];
    }
    return NULL;
}

/* ── Decode faulting instruction register ──────── */
static int hvf_decode_insn_reg(uint32_t insn)
{
    return insn & 0x1F;
}

static uint32_t hvf_read_insn(struct sv_vcpu *svc, uint64_t addr)
{
    uint64_t offset = addr - svc->vm->ram_base;
    if (offset + 4 > svc->vm->ram_size)
        return 0;
    uint32_t val;
    memcpy(&val, svc->vm->ram + offset, 4);
    return val;
}

/* ── Handle MMIO exit ──────────────────────────── */
static int hvf_handle_mmio(struct sv_vcpu *svc, sv_vcpu_exit_t *exit)
{
    uint64_t esr = svc->exit->exception.syndrome;
    uint64_t ipa = svc->exit->exception.physical_address;
    uint64_t iss = esr & 0xFFFFFF;
    unsigned ec = (unsigned)((esr >> ESR_EC_SHIFT) & ESR_EC_MASK);

    if (ec != ESR_EC_DATA_ABORT_EL0 &&
        ec != ESR_EC_DATA_ABORT_EL1 &&
        ec != ESR_EC_DATA_ABORT_EL2) {
        return -1;
    }

    unsigned size_code = (unsigned)((iss >> ISS_SIZE_SHIFT) & ISS_SIZE_MASK);
    int size = 1 << size_code;
    bool is_write = (iss >> ISS_WNR_BIT) & 1;

    hv_mmio_region_t *region = hvf_find_mmio(svc->vm, ipa);
    if (!region) {
        fprintf(stderr, "sv/hvf: unmapped MMIO at 0x%lx (size=%d, %s)\n",
                (unsigned long)ipa, size, is_write ? "W" : "R");
        exit->type = SV_EXIT_UNKNOWN;
        return 0;
    }

    uint64_t offset = ipa - region->addr;
    exit->mmio_addr = ipa;
    exit->mmio_size = size;

    if (is_write) {
        uint64_t pc;
        hv_return_t ret = hv_vcpu_get_reg(svc->id, HV_REG_PC, &pc);
        if (ret != HV_SUCCESS) {
            exit->type = SV_EXIT_UNKNOWN;
            return -1;
        }
        uint32_t insn = hvf_read_insn(svc, pc - 4);
        int reg = hvf_decode_insn_reg(insn);

        uint64_t val = 0;
        hv_vcpu_get_reg(svc->id, HV_REG_X0 + reg, &val);

        exit->type = SV_EXIT_MMIO_WRITE;
        exit->mmio_data = val;
        region->handler.write(region->handler.opaque, offset, val, size);
    } else {
        exit->type = SV_EXIT_MMIO_READ;
        exit->mmio_data = 0;

        uint64_t val = region->handler.read(region->handler.opaque, offset, size);

        uint64_t pc;
        hv_vcpu_get_reg(svc->id, HV_REG_PC, &pc);
        uint32_t insn = hvf_read_insn(svc, pc - 4);
        int reg = hvf_decode_insn_reg(insn);

        hv_vcpu_set_reg(svc->id, HV_REG_X0 + reg, val);
    }

    return 0;
}

/* ── init ───────────────────────────────────────── */
static int hvf_init(void)
{
    uint32_t max_vcpus = 0;
    hv_return_t ret = hv_vm_get_max_vcpu_count(&max_vcpus);
    if (ret != HV_SUCCESS) {
        fprintf(stderr, "sv/hvf: Hypervisor.framework not available\n");
        return -1;
    }
    printf("sv/hvf: Hypervisor.framework ready (%u max vCPUs)\n", max_vcpus);
    return 0;
}

/* ── vm_create ──────────────────────────────────── */
static sv_vm_t* hvf_vm_create(const sv_vm_config_t *config)
{
    sv_vm_t *vm = calloc(1, sizeof(*vm));
    if (!vm) return NULL;

    vm->ram_size = config->ram_size;
    vm->ram_base = config->ram_base;
    vm->num_cpus = config->num_cpus;

    hv_vm_config_t vmc = hv_vm_config_create();
    if (!vmc) {
        fprintf(stderr, "sv/hvf: failed to create VM config\n");
        free(vm);
        return NULL;
    }

    uint32_t ipa_size;
    hv_vm_config_get_default_ipa_size(&ipa_size);
    hv_vm_config_set_ipa_size(vmc, ipa_size);

    hv_return_t ret = hv_vm_create(vmc);
    os_release(vmc);

    if (ret != HV_SUCCESS) {
        fprintf(stderr, "sv/hvf: hv_vm_create failed: 0x%x\n", ret);
        free(vm);
        return NULL;
    }

    /* Use pre-allocated RAM if provided, otherwise allocate */
    if (config->preallocated_ram) {
        vm->ram = (uint8_t*)config->preallocated_ram;
    } else {
        hv_vm_allocate((void**)&vm->ram, vm->ram_size, HV_ALLOCATE_DEFAULT);
        if (!vm->ram) {
            vm->ram = malloc(vm->ram_size);
            if (!vm->ram) {
                hv_vm_destroy();
                free(vm);
                return NULL;
            }
        }
        memset(vm->ram, 0, vm->ram_size);
    }

    ret = hv_vm_map(vm->ram, (hv_ipa_t)vm->ram_base, vm->ram_size,
                    HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    if (ret != HV_SUCCESS) {
        fprintf(stderr, "sv/hvf: hv_vm_map failed: 0x%x\n", ret);
        hv_vm_destroy();
        if (!config->preallocated_ram && vm->ram)
            hv_vm_deallocate(vm->ram, vm->ram_size);
        free(vm);
        return NULL;
    }

    /* Configure GICv3 */
    hv_gic_config_t gic_config = hv_gic_config_create();
    if (!gic_config) {
        fprintf(stderr, "sv/hvf: failed to create GIC config\n");
    } else {
        hv_gic_config_set_distributor_base(gic_config, 0x08000000);
        hv_gic_config_set_redistributor_base(gic_config, 0x08010000);

        ret = hv_gic_create(gic_config);
        if (ret == HV_SUCCESS) {
            vm->gic_created = true;
            printf("sv/hvf: GICv3 created (dist=0x08000000, redis=0x08010000)\n");
        } else {
            fprintf(stderr, "sv/hvf: hv_gic_create failed: 0x%x\n", ret);
        }
        os_release(gic_config);
    }

    printf("sv/hvf: VM created (%d CPUs, %lu MB RAM)\n",
           vm->num_cpus, (unsigned long)(vm->ram_size / (1024 * 1024)));
    return vm;
}

/* ── vm_destroy ─────────────────────────────────── */
static void hvf_vm_destroy(sv_vm_t *vm)
{
    if (!vm) return;

    if (vm->ram) {
        hv_vm_unmap((hv_ipa_t)vm->ram_base, vm->ram_size);
        /* RAM is freed by machine.c — don't free it here */
    }

    hv_vm_destroy();
    free(vm);
    printf("sv/hvf: VM destroyed\n");
}

/* ── mmio_register ──────────────────────────────── */
static int hvf_mmio_register(sv_vm_t *vm, uint64_t addr, uint64_t size,
                             const sv_mmio_handler_t *handler)
{
    if (vm->num_mmio_regions >= MAX_MMIO_REGIONS) {
        fprintf(stderr, "sv/hvf: too many MMIO regions\n");
        return -1;
    }

    int idx = vm->num_mmio_regions++;
    vm->mmio_regions[idx].addr = addr;
    vm->mmio_regions[idx].size = size;
    vm->mmio_regions[idx].handler = *handler;

    printf("sv/hvf: MMIO region [%d] 0x%lx-0x%lx\n",
           idx, (unsigned long)addr, (unsigned long)(addr + size));
    return 0;
}

/* ── load_kernel ────────────────────────────────── */
static int hvf_load_kernel(sv_vm_t *vm, const char *path)
{
    (void)vm;
    if (!path) {
        /* Already loaded by machine.c into preallocated_ram */
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sv/hvf: cannot open kernel: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if ((uint64_t)size > vm->ram_size) {
        fprintf(stderr, "sv/hvf: kernel too large (%ld > %lu)\n",
                size, vm->ram_size);
        fclose(f);
        return -1;
    }

    uint64_t offset = 32 * 1024 * 1024;
    size_t nread = fread(vm->ram + offset, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        fprintf(stderr, "sv/hvf: short read on kernel\n");
        return -1;
    }

    printf("sv/hvf: kernel loaded at 0x%lx (%ld bytes)\n",
           (unsigned long)(vm->ram_base + offset), size);
    return 0;
}

/* ── load_dtb ───────────────────────────────────── */
static int hvf_load_dtb(sv_vm_t *vm, const char *path)
{
    (void)vm;
    if (!path) {
        /* Already loaded by machine.c into preallocated_ram */
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sv/hvf: cannot open DTB: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint64_t offset = 2 * 1024 * 1024;
    size_t nread = fread(vm->ram + offset, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        fprintf(stderr, "sv/hvf: short read on DTB\n");
        return -1;
    }

    printf("sv/hvf: DTB loaded at 0x%lx (%ld bytes)\n",
           (unsigned long)(vm->ram_base + offset), size);
    return 0;
}

/* ── vcpu_create ────────────────────────────────── */
static sv_vcpu_t* hvf_vcpu_create(sv_vm_t *vm, int id)
{
    sv_vcpu_t *svc = calloc(1, sizeof(*svc));
    if (!svc) return NULL;

    svc->vcpuid = id;
    svc->vm = vm;

    hv_vcpu_config_t config = hv_vcpu_config_create();

    hv_vcpu_t hvf_id;
    hv_vcpu_exit_t *exit = NULL;
    hv_return_t ret = hv_vcpu_create(&hvf_id, &exit, config);

    if (config)
        os_release(config);

    if (ret != HV_SUCCESS || !exit) {
        fprintf(stderr, "sv/hvf: hv_vcpu_create(%d) failed: 0x%x\n", id, ret);
        free(svc);
        return NULL;
    }

    svc->id = hvf_id;
    svc->exit = exit;

    printf("sv/hvf: vCPU %d created (id=0x%llx)\n", id, (unsigned long long)hvf_id);
    return svc;
}

/* ── vcpu_run ───────────────────────────────────── */
static int hvf_vcpu_run(sv_vcpu_t *svc, sv_vcpu_exit_t *exit)
{
    memset(exit, 0, sizeof(*exit));

    hv_return_t ret = hv_vcpu_run(svc->id);
    if (ret != HV_SUCCESS) {
        fprintf(stderr, "sv/hvf: hv_vcpu_run failed: 0x%x\n", ret);
        exit->type = SV_EXIT_UNKNOWN;
        return -1;
    }

    switch (svc->exit->reason) {
    case HV_EXIT_REASON_CANCELED:
        exit->type = SV_EXIT_HLT;
        break;

    case HV_EXIT_REASON_EXCEPTION: {
        uint64_t esr = svc->exit->exception.syndrome;
        uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;

        /* Data aborts = MMIO access */
        if (ec == ESR_EC_DATA_ABORT_EL0 ||
            ec == ESR_EC_DATA_ABORT_EL1 ||
            ec == ESR_EC_DATA_ABORT_EL2) {
            if (hvf_handle_mmio(svc, exit) == 0)
                break;
        }

        /* Unhandled exception */
        uint64_t ipa = svc->exit->exception.physical_address;
        uint64_t va = svc->exit->exception.virtual_address;
        fprintf(stderr, "sv/hvf: unhandled exception EC=0x%lx "
                "IPA=0x%lx VA=0x%lx ESR=0x%lx\n",
                (unsigned long)ec, (unsigned long)ipa,
                (unsigned long)va, (unsigned long)esr);
        exit->type = SV_EXIT_UNKNOWN;
        break;
    }

    case HV_EXIT_REASON_VTIMER_ACTIVATED:
        /* Guest VTimer fired — let kernel handle it via GIC */
        /* Mask is set automatically by HVF; VMM should unmask on EOI */
        exit->type = SV_EXIT_UNKNOWN;
        break;

    default:
        fprintf(stderr, "sv/hvf: unknown exit reason %u\n",
                svc->exit->reason);
        exit->type = SV_EXIT_UNKNOWN;
        break;
    }

    return 0;
}

/* ── vcpu_get_reg ───────────────────────────────── */
static int hvf_vcpu_get_reg(sv_vcpu_t *svc, uint64_t reg, uint64_t *val)
{
    if (reg <= 31) {
        hv_reg_t hvf_reg = HV_REG_X0 + (hv_reg_t)reg;
        return hv_vcpu_get_reg(svc->id, hvf_reg, val) == HV_SUCCESS ? 0 : -1;
    }
    if (reg == 31) /* PC */
        return hv_vcpu_get_reg(svc->id, HV_REG_PC, val) == HV_SUCCESS ? 0 : -1;
    if (reg == 32) /* CPSR */
        return hv_vcpu_get_reg(svc->id, HV_REG_CPSR, val) == HV_SUCCESS ? 0 : -1;
    return -1;
}

/* ── vcpu_set_reg ───────────────────────────────── */
static int hvf_vcpu_set_reg(sv_vcpu_t *svc, uint64_t reg, uint64_t val)
{
    if (reg <= 30) {
        hv_reg_t hvf_reg = HV_REG_X0 + (hv_reg_t)reg;
        return hv_vcpu_set_reg(svc->id, hvf_reg, val) == HV_SUCCESS ? 0 : -1;
    }
    if (reg == 31) /* PC */
        return hv_vcpu_set_reg(svc->id, HV_REG_PC, val) == HV_SUCCESS ? 0 : -1;
    if (reg == 32) /* CPSR */
        return hv_vcpu_set_reg(svc->id, HV_REG_CPSR, val) == HV_SUCCESS ? 0 : -1;
    return -1;
}

/* ── shutdown ───────────────────────────────────── */
static void hvf_shutdown(void)
{
    /* Resources destroyed per-VM */
}

/* ── Backend ops ────────────────────────────────── */
static const sv_hv_ops_t hvf_ops = {
    .name = "hvf",
    .type = SV_HV_HVF,
    .init = hvf_init,
    .vm_create = hvf_vm_create,
    .vm_destroy = hvf_vm_destroy,
    .mmio_register = hvf_mmio_register,
    .load_kernel = hvf_load_kernel,
    .load_dtb = hvf_load_dtb,
    .vcpu_create = hvf_vcpu_create,
    .vcpu_run = hvf_vcpu_run,
    .vcpu_get_reg = hvf_vcpu_get_reg,
    .vcpu_set_reg = hvf_vcpu_set_reg,
    .shutdown = hvf_shutdown,
};

__attribute__((constructor))
static void register_hvf(void)
{
    sv_hv_register(&hvf_ops);
    printf("sv/hvf: backend registered\n");
}

#endif /* __arm64__ && __APPLE__ */
