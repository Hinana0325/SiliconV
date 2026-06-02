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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* Internal VM structure */
struct sv_vm {
    int         kvm_fd;
    int         vm_fd;
    uint8_t    *ram;
    uint64_t    ram_size;
    uint64_t    ram_base;
    sv_vm_config_t config;
};

/* Internal vCPU structure */
struct sv_vcpu {
    int         fd;
    struct kvm_run *run;
    uint64_t    run_size;
    sv_vm_t    *vm;
    int         id;
};

/* KVM initialization */
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

/* Create VM */
static sv_vm_t* kvm_vm_create(const sv_vm_config_t *config)
{
    sv_vm_t *vm = calloc(1, sizeof(*vm));
    if (!vm) return NULL;

    vm->config = *config;
    vm->ram_size = config->ram_size;
    vm->ram_base = config->ram_base;

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

    /* TODO: Create GICv3 via KVM_CREATE_DEVICE */
    /* TODO: Create vCPU timer via KVM_ARM_VCPU_TIMER */

    return vm;

err:
    if (!config->preallocated_ram && vm->ram && vm->ram != MAP_FAILED)
        munmap(vm->ram, vm->ram_size);
    if (vm->vm_fd >= 0) close(vm->vm_fd);
    if (vm->kvm_fd >= 0) close(vm->kvm_fd);
    free(vm);
    return NULL;
}

/* Destroy VM */
static void kvm_vm_destroy(sv_vm_t *vm)
{
    if (!vm) return;
    if (vm->ram && !vm->config.preallocated_ram)
        munmap(vm->ram, vm->ram_size);
    if (vm->vm_fd >= 0) close(vm->vm_fd);
    if (vm->kvm_fd >= 0) close(vm->kvm_fd);
    free(vm);
}

/* Load kernel into guest RAM */
static int kvm_load_kernel(sv_vm_t *vm, const char *path)
{
    /* Kernel already loaded by machine.c if path is NULL */
    if (!path)
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sv: cannot open kernel: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if ((uint64_t)size > vm->ram_size) {
        fprintf(stderr, "sv: kernel too large (%ld > %lu)\n", size, vm->ram_size);
        fclose(f);
        return -1;
    }

    /* Load at RAM base + 32MB offset (room for DTB and initrd) */
    uint64_t offset = 32 * 1024 * 1024;
    size_t nread = fread(vm->ram + offset, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        fprintf(stderr, "sv: short read on kernel\n");
        return -1;
    }

    printf("sv: loaded kernel %s at 0x%lx (%ld bytes)\n",
           path, vm->ram_base + offset, size);
    return 0;
}

/* Load DTB into guest RAM */
static int kvm_load_dtb(sv_vm_t *vm, const char *path)
{
    /* DTB already loaded by machine.c if path is NULL */
    if (!path)
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sv: cannot open DTB: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Load DTB at RAM base + 2MB */
    uint64_t offset = 2 * 1024 * 1024;
    size_t nread = fread(vm->ram + offset, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        fprintf(stderr, "sv: short read on DTB\n");
        return -1;
    }

    printf("sv: loaded DTB %s at 0x%lx (%ld bytes)\n",
           path, vm->ram_base + offset, size);
    return 0;
}

/* Create vCPU */
static sv_vcpu_t* kvm_vcpu_create(sv_vm_t *vm, int id)
{
    sv_vcpu_t *vcpu = calloc(1, sizeof(*vcpu));
    if (!vcpu) return NULL;

    vcpu->vm = vm;
    vcpu->id = id;

    vcpu->fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, id);
    if (vcpu->fd < 0) {
        fprintf(stderr, "sv: failed to create vCPU %d\n", id);
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

    return vcpu;
}

/* Run vCPU */
static int kvm_vcpu_run(sv_vcpu_t *vcpu, sv_vcpu_exit_t *exit)
{
    int ret = ioctl(vcpu->fd, KVM_RUN, 0);
    if (ret < 0) {
        exit->type = SV_EXIT_UNKNOWN;
        return -1;
    }

    switch (vcpu->run->exit_reason) {
    case KVM_EXIT_MMIO:
        if (vcpu->run->mmio.is_write) {
            exit->type = SV_EXIT_MMIO_WRITE;
            exit->mmio_data = 0;
            memcpy(&exit->mmio_data, vcpu->run->mmio.data,
                   vcpu->run->mmio.len);
        } else {
            exit->type = SV_EXIT_MMIO_READ;
        }
        exit->mmio_addr = vcpu->run->mmio.phys_addr;
        exit->mmio_size = vcpu->run->mmio.len;
        break;

    case KVM_EXIT_HLT:
        exit->type = SV_EXIT_HLT;
        break;

    default:
        exit->type = SV_EXIT_UNKNOWN;
        break;
    }

    return 0;
}

/* Register the KVM backend */
static const sv_hv_ops_t kvm_ops = {
    .name = "kvm",
    .type = SV_HV_KVM,
    .init = kvm_init,
    .vm_create = kvm_vm_create,
    .vm_destroy = kvm_vm_destroy,
    .mmio_register = NULL,  /* TODO */
    .load_kernel = kvm_load_kernel,
    .load_dtb = kvm_load_dtb,
    .vcpu_create = kvm_vcpu_create,
    .vcpu_run = kvm_vcpu_run,
    .vcpu_get_reg = NULL,   /* TODO */
    .vcpu_set_reg = NULL,   /* TODO */
    .shutdown = NULL,
};

__attribute__((constructor))
static void register_kvm(void)
{
    sv_hv_register(&kvm_ops);
}

#endif /* __aarch64__ */
