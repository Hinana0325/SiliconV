/*
 * SiliconV — VM Machine (Implementation)
 *
 * The main loop. This is where the hypervisor spends most of its time.
 * Guest MMIO accesses are dispatched to the appropriate device emulator.
 *
 * On ARM64 hosts with KVM or HVF, the actual vCPU execution loop uses
 * the hypervisor abstraction layer. On other hosts, this runs as a
 * placeholder that shows the intended flow.
 */

#include "machine.h"
#include "bootimg.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/* ── MMIO handler wrapper (hv_ops ← machine) ──── */
typedef struct {
    sv_machine_t *machine;
    uint64_t base;
} mmio_wrapper_t;

static uint64_t mmio_read_wrapper(void *opaque, uint64_t offset, int size)
{
    mmio_wrapper_t *w = (mmio_wrapper_t *)opaque;
    return sv_mmio_read(w->machine, w->base + offset, size);
}

static void mmio_write_wrapper(void *opaque, uint64_t offset,
                                uint64_t value, int size)
{
    mmio_wrapper_t *w = (mmio_wrapper_t *)opaque;
    sv_mmio_write(w->machine, w->base + offset, value, size);
}

static mmio_wrapper_t mmio_wrappers[8];
static int mmio_wrapper_count = 0;

static sv_mmio_handler_t make_mmio_handler(sv_machine_t *vm, uint64_t base)
{
    if (mmio_wrapper_count >= 8)
        return (sv_mmio_handler_t){NULL, NULL, NULL};

    int idx = mmio_wrapper_count++;
    mmio_wrappers[idx].machine = vm;
    mmio_wrappers[idx].base = base;

    return (sv_mmio_handler_t){
        .read = mmio_read_wrapper,
        .write = mmio_write_wrapper,
        .opaque = &mmio_wrappers[idx],
    };
}

/* ── UART TX callback ──────────────────────────── */
static void machine_uart_tx(uint8_t byte, void *opaque)
{
    (void)opaque;
    putchar(byte);
    fflush(stdout);
}

/* ── GIC callback (IRQ pending notification) ───── */
static void machine_gic_irq(int cpu, int irq, void *opaque)
{
    sv_machine_t *vm = (sv_machine_t*)opaque;
    /* KVM/HVF — kick the vCPU out of WFI via the backend */
    (void)cpu;
    (void)irq;
    (void)vm;
}

/* ── Initialization ────────────────────────────── */

int sv_machine_init(sv_machine_t *vm, int num_cpus, uint64_t ram_size)
{
    memset(vm, 0, sizeof(*vm));
    vm->num_cpus = num_cpus;
    vm->ram_size = ram_size;
    vm->ram_base = 0x400000000ULL;

    /* Allocate guest RAM */
    vm->ram = calloc(1, ram_size);
    if (!vm->ram) {
        fprintf(stderr, "sv: failed to allocate %lu MB guest RAM\n",
                (unsigned long)(ram_size / (1024 * 1024)));
        return -1;
    }

    /* Initialize device models (used by MMIO dispatch) */
    gic_init(&vm->gic, num_cpus);
    gic_set_callback(&vm->gic, machine_gic_irq, vm);

    psci_init(&vm->psci, num_cpus);

    pl011_init(&vm->uart, 32);
    pl011_set_tx_callback(&vm->uart, machine_uart_tx, NULL);

    vm->dtb_config = dtb_config_default(num_cpus, ram_size);

    vm->running = false;

    /* Initialize hypervisor backends */
    const sv_hv_ops_t *hv = sv_hv_get_best();
    if (hv && hv->init) {
        hv->init();
    }

    printf("sv: machine initialized (%d CPUs, %lu MB RAM)\n",
           num_cpus, (unsigned long)(ram_size / (1024 * 1024)));

    return 0;
}

void sv_machine_destroy(sv_machine_t *vm)
{
    if (vm->virtio_blk_enabled)
        virtio_blk_destroy(&vm->virtio_blk);

    if (vm->ram)
        free(vm->ram);

    memset(vm, 0, sizeof(*vm));
}

/* ── Kernel Loading ────────────────────────────── */

int sv_machine_load_kernel(sv_machine_t *vm, const char *path)
{
    /* Try Android boot image first */
    sv_bootimg_t bootimg;
    if (sv_bootimg_load(&bootimg, path) == 0) {
        printf("sv: loaded Android boot image v%d\n", bootimg.version);
        sv_bootimg_dump(&bootimg);

        /* Copy kernel to guest RAM */
        uint64_t kernel_offset = 32 * 1024 * 1024;  /* 32MB offset */
        memcpy(vm->ram + kernel_offset, bootimg.kernel, bootimg.kernel_size);
        vm->kernel_entry = vm->ram_base + kernel_offset;

        /* Copy ramdisk if present */
        if (bootimg.ramdisk && bootimg.ramdisk_size > 0) {
            uint64_t rd_offset = kernel_offset +
                ((bootimg.kernel_size + 0x1FFFFF) & ~0x1FFFFF);  /* 2MB align */
            memcpy(vm->ram + rd_offset, bootimg.ramdisk, bootimg.ramdisk_size);
            printf("sv: ramdisk at 0x%lx (%u bytes)\n",
                   (unsigned long)(vm->ram_base + rd_offset),
                   bootimg.ramdisk_size);
        }

        /* Use embedded DTB if present */
        if (bootimg.dtb && bootimg.dtb_size > 0) {
            uint64_t dtb_off = 2 * 1024 * 1024;  /* 2MB offset */
            memcpy(vm->ram + dtb_off, bootimg.dtb, bootimg.dtb_size);
            vm->dtb_addr = vm->ram_base + dtb_off;
        }

        /* Update cmdline */
        if (bootimg.cmdline[0]) {
            vm->dtb_config.cmdline = strdup(bootimg.cmdline);
        }

        sv_bootimg_free(&bootimg);
        return 0;
    }

    /* Not a boot image — try raw kernel binary */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sv: cannot open kernel: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if ((uint64_t)size > vm->ram_size - 64 * 1024 * 1024) {
        fprintf(stderr, "sv: kernel too large (%ld bytes)\n", size);
        fclose(f);
        return -1;
    }

    uint64_t offset = 32 * 1024 * 1024;
    size_t nread = fread(vm->ram + offset, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        fprintf(stderr, "sv: short read on kernel\n");
        return -1;
    }

    vm->kernel_entry = vm->ram_base + offset;
    printf("sv: loaded raw kernel at 0x%lx (%ld bytes)\n",
           (unsigned long)vm->kernel_entry, size);

    return 0;
}

/* ── DTB ───────────────────────────────────────── */

int sv_machine_load_dtb(sv_machine_t *vm, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "sv: cannot open DTB: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint64_t offset = 2 * 1024 * 1024;  /* 2MB */
    size_t nread = fread(vm->ram + offset, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        fprintf(stderr, "sv: short read on DTB\n");
        return -1;
    }

    vm->dtb_addr = vm->ram_base + offset;
    printf("sv: loaded DTB at 0x%lx (%ld bytes)\n",
           (unsigned long)vm->dtb_addr, size);

    return 0;
}

int sv_machine_generate_dtb(sv_machine_t *vm)
{
    uint8_t dtb_buf[16384];
    int dtb_size = dtb_generate(dtb_buf, sizeof(dtb_buf), &vm->dtb_config);

    if (dtb_size <= 0) {
        fprintf(stderr, "sv: DTB generation failed\n");
        return -1;
    }

    uint64_t offset = 2 * 1024 * 1024;
    memcpy(vm->ram + offset, dtb_buf, dtb_size);
    vm->dtb_addr = vm->ram_base + offset;

    printf("sv: generated DTB (%d bytes) at 0x%lx\n",
           dtb_size, (unsigned long)vm->dtb_addr);

    return 0;
}

/* ── Virtio Devices ────────────────────────────── */

int sv_machine_attach_virtio_blk(sv_machine_t *vm, const char *image_path,
                                  bool read_only)
{
    int ret = virtio_blk_init(&vm->virtio_blk, image_path, read_only,
                              40,  /* IRQ 40 per spec */
                              vm->ram, vm->ram_base, vm->ram_size);
    if (ret == 0) {
        vm->virtio_blk_enabled = true;
        vm->dtb_config.virtio[0].base = 0x20000000;
        vm->dtb_config.virtio[0].irq = 40;
        vm->dtb_config.virtio[0].device_id = 2;
    }
    return ret;
}

/* ── MMIO Dispatch ─────────────────────────────── */

uint64_t sv_mmio_read(sv_machine_t *vm, uint64_t addr, int size)
{
    /* GIC Distributor: 0x08000000 - 0x0800FFFF */
    if (addr >= 0x08000000 && addr < 0x08010000) {
        return gicd_mmio_read(&vm->gic, addr - 0x08000000, size);
    }

    /* GIC Redistributor: 0x08010000 - 0x0801FFFF */
    if (addr >= 0x08010000 && addr < 0x08020000) {
        int cpu = 0;  /* TODO: determine which CPU is accessing */
        return gicr_mmio_read(&vm->gic, cpu, addr - 0x08010000, size);
    }

    /* UART0: 0x10000000 - 0x1000FFFF */
    if (addr >= 0x10000000 && addr < 0x10010000) {
        return pl011_mmio_read(&vm->uart, addr - 0x10000000, size);
    }

    /* Virtio-BLK: 0x20000000 - 0x2000FFFF */
    if (addr >= 0x20000000 && addr < 0x20010000 && vm->virtio_blk_enabled) {
        return virtio_mmio_read(&vm->virtio_blk.vdev,
                                addr - 0x20000000, size);
    }

    fprintf(stderr, "sv: unmapped MMIO read at 0x%lx (size=%d)\n",
            (unsigned long)addr, size);
    return 0;
}

void sv_mmio_write(sv_machine_t *vm, uint64_t addr, uint64_t value, int size)
{
    /* GIC Distributor */
    if (addr >= 0x08000000 && addr < 0x08010000) {
        gicd_mmio_write(&vm->gic, addr - 0x08000000, value, size);
        return;
    }

    /* GIC Redistributor */
    if (addr >= 0x08010000 && addr < 0x08020000) {
        int cpu = 0;
        gicr_mmio_write(&vm->gic, cpu, addr - 0x08010000, value, size);
        return;
    }

    /* UART0 */
    if (addr >= 0x10000000 && addr < 0x10010000) {
        pl011_mmio_write(&vm->uart, addr - 0x10000000, value, size);
        return;
    }

    /* Virtio-BLK */
    if (addr >= 0x20000000 && addr < 0x20010000 && vm->virtio_blk_enabled) {
        virtio_mmio_write(&vm->virtio_blk.vdev,
                          addr - 0x20000000, value, size);
        return;
    }

    fprintf(stderr, "sv: unmapped MMIO write at 0x%lx = 0x%lx (size=%d)\n",
            (unsigned long)addr, (unsigned long)value, size);
}

/* ── vCPU boot helpers ─────────────────────────── */
/* ARM64 Linux boot protocol: PC = kernel_entry, x0 = dtb_addr */
#define CPSR_EL1h_DAIF_MASKED 0x3C5

static int setup_vcpu(const sv_hv_ops_t *hv, sv_vcpu_t *vcpu,
                       uint64_t kernel_entry, uint64_t dtb_addr,
                       int cpu_id)
{
    if (hv->vcpu_set_reg(vcpu, 0, dtb_addr) < 0)  /* x0 = DTB */
        return -1;
    if (hv->vcpu_set_reg(vcpu, 1, cpu_id) < 0)    /* x1 = CPU ID */
        return -1;
    if (hv->vcpu_set_reg(vcpu, 31, kernel_entry) < 0)  /* PC */
        return -1;
    if (hv->vcpu_set_reg(vcpu, 32, CPSR_EL1h_DAIF_MASKED) < 0)  /* CPSR */
        return -1;
    return 0;
}

/* ── Main Loop ─────────────────────────────────── */

int sv_machine_run(sv_machine_t *vm)
{
    if (!vm->kernel_entry) {
        fprintf(stderr, "sv: no kernel loaded\n");
        return -1;
    }

    if (!vm->dtb_addr) {
        if (sv_machine_generate_dtb(vm) < 0)
            return -1;
    }

    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  SiliconV v0.1 — Starting VM\n");
    printf("  Kernel: 0x%lx\n", (unsigned long)vm->kernel_entry);
    printf("  DTB:    0x%lx\n", (unsigned long)vm->dtb_addr);
    printf("  CPUs:   %d\n", vm->num_cpus);
    printf("  RAM:    %lu MB\n",
           (unsigned long)(vm->ram_size / (1024 * 1024)));
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\n");

    /* Check for hypervisor backend */
    const sv_hv_ops_t *hv = sv_hv_get_best();
    if (!hv) {
        /* No real backend — show placeholder */
        vm->running = true;
        printf("sv: [placeholder] main loop — "
               "no hypervisor backend available\n");
        printf("sv: on ARM64: vCPU would execute at 0x%lx "
               "with DTB at 0x%lx\n",
               (unsigned long)vm->kernel_entry,
               (unsigned long)vm->dtb_addr);
        vm->running = false;
        return 0;
    }

    printf("sv: using backend '%s'\n", hv->name);

    /* ── Create VM ──────────────────────────────── */
    sv_vm_config_t cfg = sv_vm_config_default();
    cfg.num_cpus = vm->num_cpus;
    cfg.ram_size = vm->ram_size;
    cfg.ram_base = vm->ram_base;
    cfg.preallocated_ram = vm->ram;
    cfg.kernel_entry = vm->kernel_entry;
    cfg.dtb_addr = vm->dtb_addr;
    cfg.cmdline = vm->dtb_config.cmdline;

    sv_vm_t *hvm = hv->vm_create(&cfg);
    if (!hvm) {
        fprintf(stderr, "sv: VM creation failed\n");
        return -1;
    }

    /* ── Register MMIO regions ──────────────────── */
    sv_mmio_handler_t uart_h = make_mmio_handler(vm, 0x10000000);
    if (hv->mmio_register) {
        hv->mmio_register(hvm, 0x10000000, 0x10000, &uart_h);
    }

    sv_mmio_handler_t blk_h = make_mmio_handler(vm, 0x20000000);
    if (vm->virtio_blk_enabled && hv->mmio_register) {
        hv->mmio_register(hvm, 0x20000000, 0x10000, &blk_h);
    }

    /* ── Load kernel/DTB into backend RAM ───────── */
    if (hv->load_kernel)
        hv->load_kernel(hvm, NULL);
    if (hv->load_dtb)
        hv->load_dtb(hvm, NULL);

    /* ── Create vCPUs and initialize ────────────── */
    sv_vcpu_t *vcpus[8];
    int num_vcpus = vm->num_cpus;
    if (num_vcpus > 8) num_vcpus = 8;

    for (int i = 0; i < num_vcpus; i++) {
        vcpus[i] = hv->vcpu_create(hvm, i);
        if (!vcpus[i]) {
            fprintf(stderr, "sv: vCPU %d creation failed\n", i);
            for (int j = 0; j < i; j++)
                /* no vcpu_destroy op — rely on vm_destroy */;
            hv->vm_destroy(hvm);
            return -1;
        }

        if (setup_vcpu(hv, vcpus[i],
                       vm->kernel_entry, vm->dtb_addr, i) < 0) {
            fprintf(stderr, "sv: vCPU %d setup failed\n", i);
            hv->vm_destroy(hvm);
            return -1;
        }
    }

    /* ── Run loop (vCPU 0) ──────────────────────── */
    vm->running = true;
    sv_vcpu_exit_t exit_info;
    int ret = 0;

    printf("sv: entering main loop\n");

    while (vm->running) {
        ret = hv->vcpu_run(vcpus[0], &exit_info);
        if (ret < 0) {
            fprintf(stderr, "sv: vCPU run error\n");
            break;
        }

        switch (exit_info.type) {
        case SV_EXIT_MMIO_READ:
        case SV_EXIT_MMIO_WRITE:
            /* Handled internally by the HVF backend */
            break;

        case SV_EXIT_HLT:
            printf("sv: guest HLT\n");
            vm->running = false;
            break;

        case SV_EXIT_SHUTDOWN:
            printf("sv: guest shutdown\n");
            vm->running = false;
            break;

        case SV_EXIT_UNKNOWN:
            /* Most VM exits are transparently handled by the backend */
            break;
        }
    }

    hv->vm_destroy(hvm);
    printf("sv: VM stopped\n");
    return ret;
}

void sv_machine_stop(sv_machine_t *vm)
{
    vm->running = false;
    printf("sv: VM stopped\n");
}
