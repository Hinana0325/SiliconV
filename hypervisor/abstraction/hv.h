/*
 * SiliconV — Hypervisor Abstraction Layer
 *
 * Defines the interface that all hypervisor backends must implement.
 * Backends: KVM (Linux), HVF (macOS), WHPX (Windows)
 */

#ifndef SILICONV_HV_H
#define SILICONV_HV_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
typedef struct sv_vm sv_vm_t;
typedef struct sv_vcpu sv_vcpu_t;

/* Hypervisor backend type */
typedef enum {
    SV_HV_KVM,      /* Linux KVM */
    SV_HV_HVF,      /* macOS Hypervisor.framework */
    SV_HV_WHPX,     /* Windows Hypervisor Platform */
} sv_hv_type_t;

/* VM configuration */
typedef struct {
    int         num_cpus;       /* Number of vCPUs (default: 4) */
    uint64_t    ram_size;       /* Guest RAM in bytes (default: 4G) */
    uint64_t    ram_base;       /* Guest RAM base address (default: 0x400000000) */
    const char *kernel_path;    /* Path to kernel image (direct boot) */
    const char *dtb_path;       /* Path to DTB (optional, generated if NULL) */
    const char *initrd_path;    /* Path to initramfs (optional) */
    const char *cmdline;        /* Kernel command line */

    /* Pre-loaded data from machine.c (optional, takes priority over paths) */
    void       *preallocated_ram;  /* If non-NULL: use this as guest RAM */
    uint64_t    kernel_entry;      /* Pre-loaded kernel entry (if preallocated_ram set) */
    uint64_t    dtb_addr;          /* Pre-loaded DTB IPA (if preallocated_ram set) */
    uint64_t    initrd_addr;       /* Pre-loaded initrd IPA (0 if none) */

    /* MMIO dispatch callbacks (machine.c → backend) */
    uint64_t (*mmio_read)(void *opaque, uint64_t addr, int size);
    void     (*mmio_write)(void *opaque, uint64_t addr, uint64_t value, int size);
    void      *callback_opaque;    /* Passed to mmio_read/write */
} sv_vm_config_t;

/* MMIO handler callbacks */
typedef struct {
    uint64_t (*read)(void *opaque, uint64_t offset, int size);
    void     (*write)(void *opaque, uint64_t offset, uint64_t value, int size);
    void     *opaque;
} sv_mmio_handler_t;

/* vCPU exit reason */
typedef enum {
    SV_EXIT_MMIO_READ,
    SV_EXIT_MMIO_WRITE,
    SV_EXIT_HLT,
    SV_EXIT_SHUTDOWN,
    SV_EXIT_UNKNOWN,
} sv_exit_type_t;

/* vCPU exit info */
typedef struct {
    sv_exit_type_t type;
    int            vcpu_id;
    uint64_t       mmio_addr;
    uint64_t       mmio_data;
    int            mmio_size;
} sv_vcpu_exit_t;

/* Hypervisor backend operations */
typedef struct {
    const char *name;
    sv_hv_type_t type;

    /* Initialize the hypervisor */
    int  (*init)(void);

    /* Create a VM */
    sv_vm_t* (*vm_create)(const sv_vm_config_t *config);

    /* Destroy a VM */
    void (*vm_destroy)(sv_vm_t *vm);

    /* Register MMIO region */
    int  (*mmio_register)(sv_vm_t *vm, uint64_t addr, uint64_t size,
                          const sv_mmio_handler_t *handler);

    /* Load kernel image into guest memory */
    int  (*load_kernel)(sv_vm_t *vm, const char *path);

    /* Load DTB into guest memory */
    int  (*load_dtb)(sv_vm_t *vm, const char *path);

    /* Create a vCPU */
    sv_vcpu_t* (*vcpu_create)(sv_vm_t *vm, int id);

    /* Run a vCPU until exit */
    int  (*vcpu_run)(sv_vcpu_t *vcpu, sv_vcpu_exit_t *exit);

    /* Get/set vCPU register */
    int  (*vcpu_get_reg)(sv_vcpu_t *vcpu, uint64_t reg, uint64_t *val);
    int  (*vcpu_set_reg)(sv_vcpu_t *vcpu, uint64_t reg, uint64_t val);

    /* Shutdown */
    void (*shutdown)(void);
} sv_hv_ops_t;

/* Register a hypervisor backend */
int sv_hv_register(const sv_hv_ops_t *ops);

/* Get the best available backend */
const sv_hv_ops_t* sv_hv_get_best(void);

/* Default VM config */
sv_vm_config_t sv_vm_config_default(void);

#endif /* SILICONV_HV_H */
