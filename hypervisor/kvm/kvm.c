/*
 * SiliconV — KVM Backend (Linux ARM64)
 *
 * Implements the hypervisor abstraction using Linux KVM API.
 * Creates ARM64 virtual machines with GICv3 and device emulation.
 *
 * Requires: Linux, /dev/kvm, ARM64 host
 */

#include "hv.h"

#ifdef __aarch64__

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ── ARM64 KVM Register Encoding ───────────────── */
#define KVM_REG_ARM64_CORE_REG(n) \
    (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM64_CORE | (n))

/* Core register offsets (from asm/kvm.h) */
#define KVM_ARM64_REG_X0    0
#define KVM_ARM64_REG_SP    31
#define KVM_ARM64_REG_PC    32
#define KVM_ARM64_REG_PSTATE 33

/* Internal VM structure */
struct sv_vm {
    int         kvm_fd;
    int         vm_fd;
    uint8_t    *ram;
    uint64_t    ram_size;
    uint64_t    ram_base;
    sv_vm_config_t config;

    /* GIC device fd */
    int         gic_fd;

    /* MMIO dispatch callbacks (from machine.c) */
    uint64_t (*mmio_read)(void *opaque, uint64_t addr, int size);
    void     (*mmio_write)(void *opaque, uint64_t addr, uint64_t value, int size);
    void      *mmio_opaque;
};

/* Internal vCPU structure */
struct sv_vcpu {
    int         fd;
    struct kvm_run *run;
    uint64_t    run_size;
    sv_vm_t    *vm;
    int         id;
};

/* ── KVM initialization ────────────────────────── */

static int kvm_init(void)
{
    int fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "sv: cannot open /dev/kvm: %s\n", strerror(errno));
        return -1;
    }

    /* Check API version */
    int ret = ioctl(fd, KVM_GET_API_VERSION, 0);
    if (ret != KVM_API_VERSION) {
        fprintf(stderr, "sv: KVM API version mismatch: %d vs %d\n",
                ret, KVM_API_VERSION);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/* ── GICv3 setup via KVM_CREATE_DEVICE ─────────── */

static int kvm_create_gic(sv_vm_t *vm)
{
    struct kvm_create_device cd = {
        .type = KVM_DEV_TYPE_ARM_VGIC_V3,
        .fd = 0,
        .flags = 0,
    };

    if (ioctl(vm->vm_fd, KVM_CREATE_DEVICE, &cd) < 0) {
        fprintf(stderr, "sv: failed to create GICv3: %s (continuing without)\n",
                strerror(errno));
        vm->gic_fd = -1;
        return -1;
    }

    vm->gic_fd = cd.fd;

    /* Set GIC attributes: init GICv3 */
    /* Distributor address */
    struct kvm_device_attr attr = {
        .group = KVM_DEV_ARM_VGIC_GRP_ADDR,
        .attr = KVM_VGIC_V3_ADDR_TYPE_DIST,
        .addr = (uint64_t)(unsigned long)&(uint64_t){0x08000000},
    };
    if (ioctl(vm->gic_fd, KVM_SET_DEVICE_ATTR, &attr) < 0) {
        fprintf(stderr, "sv: failed to set GIC dist addr: %s\n", strerror(errno));
    }

    /* Redistributor address */
    attr.attr = KVM_VGIC_V3_ADDR_TYPE_REDIST;
    uint64_t redist_base = 0x08010000;
    attr.addr = (uint64_t)(unsigned long)&redist_base;
    if (ioctl(vm->gic_fd, KVM_SET_DEVICE_ATTR, &attr) < 0) {
        fprintf(stderr, "sv: failed to set GIC redist addr: %s\n", strerror(errno));
    }

    /* Initialize GIC */
    attr.group = KVM_DEV_ARM_VGIC_GRP_CTRL;
    attr.attr = KVM_VGIC_CTRL_INIT;
    attr.addr = 0;
    if (ioctl(vm->gic_fd, KVM_SET_DEVICE_ATTR, &attr) < 0) {
        fprintf(stderr, "sv: failed to init GIC: %s\n", strerror(errno));
    }

    printf("sv: GICv3 created via KVM\n");
    return 0;
}

/* ── Create VM ─────────────────────────────────── */

static sv_vm_t* kvm_vm_create(const sv_vm_config_t *config)
{
    sv_vm_t *vm = calloc(1, sizeof(*vm));
    if (!vm) return NULL;

    vm->config = *config;
    vm->ram_size = config->ram_size;
    vm->ram_base = config->ram_base;
    vm->gic_fd = -1;

    /* Store MMIO callbacks */
    vm->mmio_read = config->mmio_read;
    vm->mmio_write = config->mmio_write;
    vm->mmio_opaque = config->callback_opaque;

    /* Open KVM device */
    vm->kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (vm->kvm_fd < 0) goto err;

    /* Create VM */
    vm->vm_fd = ioctl(vm->kvm_fd, KVM_CREATE_VM, 0);
    if (vm->vm_fd < 0) goto err;

    /* Use pre-allocated RAM if provided, otherwise allocate */
    if (config->preallocated_ram) {
        vm->ram = (uint8_t*)config->preallocated_ram;
    } else {
        vm->ram = mmap(NULL, vm->ram_size,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                       -1, 0);
        if (vm->ram == MAP_FAILED) goto err;
    }

    /* Map RAM into guest physical address space */
    struct kvm_userspace_memory_region mem = {
        .slot = 0,
        .flags = 0,
        .guest_phys_addr = vm->ram_base,
        .memory_size = vm->ram_size,
        .userspace_addr = (uint64_t)vm->ram,
    };

    if (ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &mem) < 0) {
        fprintf(stderr, "sv: failed to set guest memory\n");
        goto err;
    }

    /* Create GICv3 (best effort) */
    kvm_create_gic(vm);

    return vm;

err:
    if (!config->preallocated_ram && vm->ram && vm->ram != MAP_FAILED)
        munmap(vm->ram, vm->ram_size);
    if (vm->vm_fd >= 0) close(vm->vm_fd);
    if (vm->kvm_fd >= 0) close(vm->kvm_fd);
    free(vm);
    return NULL;
}

/* ── Destroy VM ────────────────────────────────── */

static void kvm_vm_destroy(sv_vm_t *vm)
{
    if (!vm) return;
    if (vm->gic_fd >= 0) close(vm->gic_fd);
    if (vm->ram && !vm->config.preallocated_ram)
        munmap(vm->ram, vm->ram_size);
    if (vm->vm_fd >= 0) close(vm->vm_fd);
    if (vm->kvm_fd >= 0) close(vm->kvm_fd);
    free(vm);
}

/* ── Load kernel (no-op, handled by machine.c) ── */

static int kvm_load_kernel(sv_vm_t *vm, const char *path)
{
    (void)vm;
    if (!path) return 0;

    /* Should not be called — machine.c loads kernel into preallocated RAM */
    fprintf(stderr, "sv: kvm_load_kernel called with path (unexpected)\n");
    return -1;
}

static int kvm_load_dtb(sv_vm_t *vm, const char *path)
{
    (void)vm;
    if (!path) return 0;
    fprintf(stderr, "sv: kvm_load_dtb called with path (unexpected)\n");
    return -1;
}

/* ── Register MMIO region (no-op, handled via callbacks) ── */

static int kvm_mmio_register(sv_vm_t *vm, uint64_t addr, uint64_t size,
                              const sv_mmio_handler_t *handler)
{
    /* KVM handles MMIO via KVM_EXIT_MMIO — we dispatch in the run loop.
     * The handler is already stored via the vm's callbacks.
     * We just need to make sure the memory region is NOT mapped,
     * so KVM will trap accesses. */
    (void)vm;
    (void)addr;
    (void)size;
    (void)handler;
    return 0;
}

/* ── Create vCPU ───────────────────────────────── */

static sv_vcpu_t* kvm_vcpu_create(sv_vm_t *vm, int id)
{
    sv_vcpu_t *vcpu = calloc(1, sizeof(*vcpu));
    if (!vcpu) return NULL;

    vcpu->vm = vm;
    vcpu->id = id;

    vcpu->fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, id);
    if (vcpu->fd < 0) {
        fprintf(stderr, "sv: failed to create vCPU %d: %s\n", id, strerror(errno));
        free(vcpu);
        return NULL;
    }

    /* Map the kvm_run structure */
    vcpu->run_size = ioctl(vm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    vcpu->run = mmap(NULL, vcpu->run_size,
                     PROT_READ | PROT_WRITE, MAP_SHARED,
                     vcpu->fd, 0);
    if (vcpu->run == MAP_FAILED) {
        close(vcpu->fd);
        free(vcpu);
        return NULL;
    }

    /* Enable vCPU timer */
    struct kvm_vcpu_init init = {0};
    /* Request PSCI-based CPU features */
    ioctl(vcpu->fd, KVM_GET_PREFERRED_TARGET, &init);
    ioctl(vcpu->fd, KVM_ARM_VCPU_INIT, &init);

    printf("sv: vCPU %d created\n", id);
    return vcpu;
}

/* ── Run vCPU ──────────────────────────────────── */

static int kvm_vcpu_run(sv_vcpu_t *vcpu, sv_vcpu_exit_t *exit)
{
    sv_vm_t *vm = vcpu->vm;

    int ret = ioctl(vcpu->fd, KVM_RUN, 0);
    if (ret < 0) {
        exit->type = SV_EXIT_UNKNOWN;
        exit->vcpu_id = vcpu->id;
        return -1;
    }

    exit->vcpu_id = vcpu->id;

    switch (vcpu->run->exit_reason) {
    case KVM_EXIT_MMIO: {
        uint64_t addr = vcpu->run->mmio.phys_addr;
        int size = vcpu->run->mmio.len;

        if (vcpu->run->mmio.is_write) {
            /* Guest wrote to MMIO — dispatch to device emulator */
            exit->type = SV_EXIT_MMIO_WRITE;
            exit->mmio_addr = addr;
            exit->mmio_size = size;
            memcpy(&exit->mmio_data, vcpu->run->mmio.data, size);

            /* Call machine's MMIO write handler */
            if (vm->mmio_write) {
                uint64_t val = 0;
                memcpy(&val, vcpu->run->mmio.data, size);
                vm->mmio_write(vm->mmio_opaque, addr, val, size);
            }
        } else {
            /* Guest read from MMIO — dispatch to device emulator */
            exit->type = SV_EXIT_MMIO_READ;
            exit->mmio_addr = addr;
            exit->mmio_size = size;

            if (vm->mmio_read) {
                uint64_t val = vm->mmio_read(vm->mmio_opaque, addr, size);
                memcpy(vcpu->run->mmio.data, &val, size);
            }
        }
        break;
    }

    case KVM_EXIT_HLT:
        exit->type = SV_EXIT_HLT;
        break;

    case KVM_EXIT_SYSTEM_EVENT:
        if (vcpu->run->system_event.type == KVM_SYSTEM_EVENT_SHUTDOWN) {
            exit->type = SV_EXIT_SHUTDOWN;
        } else {
            exit->type = SV_EXIT_UNKNOWN;
        }
        break;

    case KVM_EXIT_INTERNAL_ERROR:
        fprintf(stderr, "sv: vCPU %d internal error: suberror=%d\n",
                vcpu->id, vcpu->run->internal.suberror);
        exit->type = SV_EXIT_UNKNOWN;
        break;

    case KVM_EXIT_DEBUG:
        exit->type = SV_EXIT_UNKNOWN;
        break;

    default:
        fprintf(stderr, "sv: vCPU %d unknown exit reason: %d\n",
                vcpu->id, vcpu->run->exit_reason);
        exit->type = SV_EXIT_UNKNOWN;
        break;
    }

    return 0;
}

/* ── vCPU Register Access ──────────────────────── */

static int kvm_vcpu_get_reg(sv_vcpu_t *vcpu, uint64_t reg, uint64_t *val)
{
    uint64_t kvm_reg;

    /* Map our register numbering to KVM register IDs */
    if (reg <= 30) {
        /* x0-x30 */
        kvm_reg = KVM_REG_ARM64_CORE_REG(reg);
    } else if (reg == 31) {
        /* SP */
        kvm_reg = KVM_REG_ARM64_CORE_REG(KVM_ARM64_REG_SP);
    } else if (reg == 32) {
        /* PC */
        kvm_reg = KVM_REG_ARM64_CORE_REG(KVM_ARM64_REG_PC);
    } else if (reg == 33) {
        /* PSTATE/CPSR */
        kvm_reg = KVM_REG_ARM64_CORE_REG(KVM_ARM64_REG_PSTATE);
    } else {
        fprintf(stderr, "sv: unsupported register %lu\n", (unsigned long)reg);
        return -1;
    }

    struct kvm_one_reg one = {
        .id = kvm_reg,
        .addr = (uint64_t)(unsigned long)val,
    };

    if (ioctl(vcpu->fd, KVM_GET_ONE_REG, &one) < 0) {
        fprintf(stderr, "sv: failed to get reg %lu: %s\n",
                (unsigned long)reg, strerror(errno));
        return -1;
    }

    return 0;
}

static int kvm_vcpu_set_reg(sv_vcpu_t *vcpu, uint64_t reg, uint64_t val)
{
    uint64_t kvm_reg;

    if (reg <= 30) {
        kvm_reg = KVM_REG_ARM64_CORE_REG(reg);
    } else if (reg == 31) {
        kvm_reg = KVM_REG_ARM64_CORE_REG(KVM_ARM64_REG_SP);
    } else if (reg == 32) {
        kvm_reg = KVM_REG_ARM64_CORE_REG(KVM_ARM64_REG_PC);
    } else if (reg == 33) {
        kvm_reg = KVM_REG_ARM64_CORE_REG(KVM_ARM64_REG_PSTATE);
    } else {
        fprintf(stderr, "sv: unsupported register %lu\n", (unsigned long)reg);
        return -1;
    }

    struct kvm_one_reg one = {
        .id = kvm_reg,
        .addr = (uint64_t)(unsigned long)&val,
    };

    if (ioctl(vcpu->fd, KVM_SET_ONE_REG, &one) < 0) {
        fprintf(stderr, "sv: failed to set reg %lu = 0x%lx: %s\n",
                (unsigned long)reg, (unsigned long)val, strerror(errno));
        return -1;
    }

    return 0;
}

/* ── Register the KVM backend ──────────────────── */

static const sv_hv_ops_t kvm_ops = {
    .name = "kvm",
    .type = SV_HV_KVM,
    .init = kvm_init,
    .vm_create = kvm_vm_create,
    .vm_destroy = kvm_vm_destroy,
    .mmio_register = kvm_mmio_register,
    .load_kernel = kvm_load_kernel,
    .load_dtb = kvm_load_dtb,
    .vcpu_create = kvm_vcpu_create,
    .vcpu_run = kvm_vcpu_run,
    .vcpu_get_reg = kvm_vcpu_get_reg,
    .vcpu_set_reg = kvm_vcpu_set_reg,
    .shutdown = NULL,
};

__attribute__((constructor))
static void register_kvm(void)
{
    sv_hv_register(&kvm_ops);
}

#endif /* __aarch64__ */
