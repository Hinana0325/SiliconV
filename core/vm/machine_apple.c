/*
 * SiliconV — Apple Machine Profile (Implementation)
 *
 * Ties together all Apple device models and boots XNU kernels.
 * This is the Apple equivalent of machine.c but uses AIC, Apple UART,
 * DART, SEP, etc. instead of GICv3, PL011, etc.
 *
 * Supports both:
 *   - TCG backend (software emulation on x86_64)
 *   - KVM/HVF backend (hardware acceleration on ARM64)
 */

#include "machine_apple.h"
#include "../memory/mmio_addrs.h"
#include "../boot/apple/xnu_boot.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/* TCG-specific MMU helpers (included only for TCG backend) */
#include "../../hypervisor/tcg/tcg.h"
#include "../../hypervisor/tcg/tcg_mmu.h"

/* ── UART TX callback ──────────────────────────── */
static void apple_uart_tx_cb(uint8_t byte, void *opaque)
{
    (void)opaque;
    putchar(byte);
    fflush(stdout);
}

/* ── AIC IRQ callback ──────────────────────────── */
static void apple_aic_irq_cb(int cpu, int irq, void *opaque)
{
    sv_machine_apple_t *vm = (sv_machine_apple_t *)opaque;
    (void)cpu;
    (void)irq;
    (void)vm;
    /* KVM/HVF — kick vCPU out of WFI */
}

/* ── Device IRQ → AIC bridge ───────────────────── */
static void apple_dev_raise_irq(void *aic_ctx, int irq)
{
    apple_aic_state_t *aic = (apple_aic_state_t *)aic_ctx;
    apple_aic_raise_irq(aic, irq);
}

static void apple_dev_lower_irq(void *aic_ctx, int irq)
{
    apple_aic_state_t *aic = (apple_aic_state_t *)aic_ctx;
    apple_aic_lower_irq(aic, irq);
}

/* ── MMIO dispatch callbacks ────────────────────── */
static uint64_t machine_apple_mmio_read(void *opaque, uint64_t addr, int size)
{
    sv_machine_apple_t *vm = (sv_machine_apple_t *)opaque;
    return sv_mmio_apple_read(vm, addr, size);
}

static void machine_apple_mmio_write(void *opaque, uint64_t addr,
                                      uint64_t value, int size)
{
    sv_machine_apple_t *vm = (sv_machine_apple_t *)opaque;
    sv_mmio_apple_write(vm, addr, value, size);
}

/* ── MMIO dispatch using direct if-else (no function pointer casts) ── */

/* For each Apple device type, define a dispatch helper */
static uint64_t apple_dispatch_read(sv_machine_apple_t *vm, uint64_t addr, uint64_t base, uint64_t size, int which)
{
    uint64_t off = addr - base;
    if (off >= size) return 0;

    switch (which) {
    case 0:  return apple_aic_mmio_read(&vm->aic, off, 4);
    case 1:  return apple_uart_mmio_read(&vm->uart, off, 4);
    case 2:  return apple_dart_mmio_read(&vm->dart[0], off, 4);
    case 3:  return apple_dart_mmio_read(&vm->dart[1], off, 4);
    case 4:  return apple_sep_mmio_read(&vm->sep, off, 4);
    case 5:  return apple_wdt_mmio_read(&vm->wdt, off, 4);
    case 6:  return apple_nvram_mmio_read(&vm->nvram, off, 4);
    default: return 0;
    }
}

static void apple_dispatch_write(sv_machine_apple_t *vm, uint64_t addr, uint64_t value, uint64_t base, uint64_t size, int which)
{
    uint64_t off = addr - base;
    if (off >= size) return;

    switch (which) {
    case 0:  apple_aic_mmio_write(&vm->aic, off, value, 4); break;
    case 1:  apple_uart_mmio_write(&vm->uart, off, value, 4); break;
    case 2:  apple_dart_mmio_write(&vm->dart[0], off, value, 4); break;
    case 3:  apple_dart_mmio_write(&vm->dart[1], off, value, 4); break;
    case 4:  apple_sep_mmio_write(&vm->sep, off, value, 4); break;
    case 5:  apple_wdt_mmio_write(&vm->wdt, off, value, 4); break;
    case 6:  apple_nvram_mmio_write(&vm->nvram, off, value, 4); break;
    default: break;
    }
}

/* MMIO region descriptor (no function pointers) */
typedef struct {
    uint64_t    base;
    uint64_t    end;
    const char *name;
    int         dev_index;  /* Index for dispatch switch */
} apple_mmio_region_t;

static apple_mmio_region_t apple_mmio_regions[] = {
    { SV_APPLE_AIC_BASE,     SV_APPLE_AIC_END,         "aic",    0 },
    { SV_APPLE_UART0_BASE,   SV_APPLE_UART0_END,       "uart0",  1 },
    { SV_APPLE_DART0_BASE,   SV_APPLE_DART0_BASE + SV_APPLE_DART0_SIZE - 1, "dart0", 2 },
    { SV_APPLE_DART1_BASE,   SV_APPLE_DART1_BASE + SV_APPLE_DART1_SIZE - 1, "dart1", 3 },
    { SV_APPLE_SEP_BASE,     SV_APPLE_SEP_END,         "sep",    4 },
    { SV_APPLE_WDT_BASE,     SV_APPLE_WDT_BASE + SV_APPLE_WDT_SIZE - 1,     "wdt",    5 },
    { SV_APPLE_NVRAM_BASE,   SV_APPLE_NVRAM_BASE + SV_APPLE_NVRAM_SIZE - 1, "nvram",  6 },
};
#define APPLE_NUM_MMIO_REGIONS (sizeof(apple_mmio_regions)/sizeof(apple_mmio_regions[0]))

/* ── Initialization ─────────────────────────────── */
int sv_machine_apple_init(sv_machine_apple_t *vm, int num_cpus, uint64_t ram_size)
{
    memset(vm, 0, sizeof(*vm));
    vm->num_cpus = (num_cpus < 8) ? num_cpus : 8;
    vm->ram_size = ram_size;
    vm->ram_base = SV_APPLE_RAM_BASE;

    /* Allocate guest RAM */
    vm->ram = calloc(1, ram_size);
    if (!vm->ram) {
        fprintf(stderr, "sv_apple: failed to allocate %lu MB guest RAM\n",
                (unsigned long)(ram_size / (1024 * 1024)));
        return -1;
    }

    /* ── Initialize Apple device models ──────────── */
    /* AIC (interrupt controller) */
    apple_aic_init(&vm->aic, num_cpus);
    apple_aic_set_callback(&vm->aic, apple_aic_irq_cb, vm);

    /* Apple UART0 (console) — wire TX to stdout, IRQ to AIC */
    apple_uart_init(&vm->uart, SV_IRQ_APPLE_UART0, 0);
    apple_uart_set_tx_callback(&vm->uart, apple_uart_tx_cb, NULL);
    apple_uart_set_irq_ctx(&vm->uart, &vm->aic);
    apple_uart_set_irq_callbacks(&vm->uart, apple_dev_raise_irq, apple_dev_lower_irq);

    /* DART IOMMU (2 instances) */
    apple_dart_init(&vm->dart[0], 0);
    apple_dart_init(&vm->dart[1], 1);

    /* SEP (Secure Enclave) — wire IRQ to AIC */
    apple_sep_init(&vm->sep, SV_IRQ_APPLE_SEP);
    apple_sep_set_irq_ctx(&vm->sep, &vm->aic);
    apple_sep_set_irq_callbacks(&vm->sep, apple_dev_raise_irq, apple_dev_lower_irq);

    /* WDT (watchdog timer) */
    apple_wdt_init(&vm->wdt);

    /* NVRAM */
    apple_nvram_init(&vm->nvram);

    /* PSCI */
    psci_init(&vm->psci, num_cpus);

    /* ── MMIO region dispatch table ready ───────── */

    vm->running = false;

    /* Initialize hypervisor backend */
    const sv_hv_ops_t *hv = sv_hv_get_best();
    if (hv && hv->init) {
        hv->init();
    }

    printf("sv_apple: Apple VM initialized (%d CPUs, %lu MB RAM)\n",
           num_cpus, (unsigned long)(ram_size / (1024 * 1024)));
    printf("sv_apple: AIC (interrupt), S5L UART, DART, SEP, WDT, NVRAM ready\n");

    return 0;
}

void sv_machine_apple_destroy(sv_machine_apple_t *vm)
{
    if (vm->virtio_blk_enabled)
        virtio_blk_destroy(&vm->virtio_blk);
    if (vm->virtio_net_enabled)
        virtio_net_destroy(&vm->virtio_net);
    if (vm->virtio_console_enabled)
        virtio_console_destroy(&vm->virtio_console);

    if (vm->ram)
        free(vm->ram);

    memset(vm, 0, sizeof(*vm));
}

/* ── Kernel Loading ─────────────────────────────── */
int sv_machine_apple_load_kernel(sv_machine_apple_t *vm,
                                  const uint8_t *data, size_t size,
                                  const char *cmdline)
{
    xnu_boot_config_t config;
    memset(&config, 0, sizeof(config));
    config.ram_base = vm->ram_base;
    config.ram_size = vm->ram_size;
    config.boot_flags = 0x00020000; /* VERBOSE flag */
    config.cmdline = cmdline ? cmdline : "debug=0x8 serial=2 -v keepsyms=1";

    xnu_boot_init(&vm->xnu_boot, &config);

    if (xnu_boot_load_kernel(&vm->xnu_boot, data, size) < 0) {
        fprintf(stderr, "sv_apple: failed to load XNU kernel\n");
        return -1;
    }

    /* Setup boot environment in guest RAM */
    if (xnu_boot_setup(&vm->xnu_boot, vm->ram, vm->ram_base, vm->ram_size) < 0) {
        fprintf(stderr, "sv_apple: failed to setup XNU boot\n");
        return -1;
    }

    vm->kernel_entry = xnu_boot_get_entry(&vm->xnu_boot);

    xnu_boot_print_summary(&vm->xnu_boot);

    return 0;
}

int sv_machine_apple_load_dtb(sv_machine_apple_t *vm,
                               const uint8_t *data, size_t size)
{
    return xnu_boot_load_dtb(&vm->xnu_boot, data, size);
}

/* ── Virtio Devices ─────────────────────────────── */
int sv_machine_apple_attach_virtio_blk(sv_machine_apple_t *vm,
                                        const char *image_path, bool read_only)
{
    int ret = virtio_blk_init(&vm->virtio_blk, image_path, read_only,
                               SV_IRQ_APPLE_VIRTIO_BLK0,
                               vm->ram, vm->ram_base, vm->ram_size);
    if (ret == 0) {
        vm->virtio_blk_enabled = true;
    }
    return ret;
}

int sv_machine_apple_attach_virtio_net(sv_machine_apple_t *vm)
{
    int ret = virtio_net_init(&vm->virtio_net,
                               SV_IRQ_APPLE_VIRTIO_NET,
                               vm->ram, vm->ram_base, vm->ram_size);
    if (ret == 0) {
        vm->virtio_net_enabled = true;
    }
    return ret;
}

int sv_machine_apple_attach_virtio_console(sv_machine_apple_t *vm)
{
    int ret = virtio_console_init(&vm->virtio_console,
                                   SV_IRQ_APPLE_VIRTIO_CONSOLE,
                                   vm->ram, vm->ram_base, vm->ram_size);
    if (ret == 0) {
        vm->virtio_console_enabled = true;
    }
    return ret;
}

/* ── MMIO Dispatch ──────────────────────────────── */
uint64_t sv_mmio_apple_read(sv_machine_apple_t *vm, uint64_t addr, int size)
{
    (void)size;

    /* Search registered Apple MMIO regions */
    for (size_t i = 0; i < APPLE_NUM_MMIO_REGIONS; i++) {
        if (addr >= apple_mmio_regions[i].base && addr <= apple_mmio_regions[i].end) {
            return apple_dispatch_read(vm, addr,
                                       apple_mmio_regions[i].base,
                                       apple_mmio_regions[i].end - apple_mmio_regions[i].base + 1,
                                       apple_mmio_regions[i].dev_index);
        }
    }

    /* Virtio devices (dynamically attached) */
    if (vm->virtio_blk_enabled &&
        addr >= SV_ADDR_VIRTIO_BLK &&
        addr < SV_ADDR_VIRTIO_BLK + SV_ADDR_VIRTIO_SIZE) {
        return virtio_mmio_read(&vm->virtio_blk.vdev,
                                addr - SV_ADDR_VIRTIO_BLK, size);
    }

    if (vm->virtio_console_enabled &&
        addr >= SV_ADDR_VIRTIO_CONSOLE &&
        addr < SV_ADDR_VIRTIO_CONSOLE + SV_ADDR_VIRTIO_SIZE) {
        return virtio_mmio_read(&vm->virtio_console.vdev,
                                addr - SV_ADDR_VIRTIO_CONSOLE, size);
    }

    if (vm->virtio_net_enabled &&
        addr >= SV_ADDR_VIRTIO_NET &&
        addr < SV_ADDR_VIRTIO_NET + SV_ADDR_VIRTIO_SIZE) {
        return virtio_mmio_read(&vm->virtio_net.vdev,
                                addr - SV_ADDR_VIRTIO_NET, size);
    }

    fprintf(stderr, "sv_apple: unmapped MMIO read at 0x%lx (size=%d)\n",
            (unsigned long)addr, size);
    return 0;
}

void sv_mmio_apple_write(sv_machine_apple_t *vm, uint64_t addr,
                          uint64_t value, int size)
{
    (void)size;

    /* Search registered Apple MMIO regions */
    for (size_t i = 0; i < APPLE_NUM_MMIO_REGIONS; i++) {
        if (addr >= apple_mmio_regions[i].base && addr <= apple_mmio_regions[i].end) {
            apple_dispatch_write(vm, addr, value,
                                 apple_mmio_regions[i].base,
                                 apple_mmio_regions[i].end - apple_mmio_regions[i].base + 1,
                                 apple_mmio_regions[i].dev_index);
            return;
        }
    }

    /* Virtio devices (dynamically attached) */
    if (vm->virtio_blk_enabled &&
        addr >= SV_ADDR_VIRTIO_BLK &&
        addr < SV_ADDR_VIRTIO_BLK + SV_ADDR_VIRTIO_SIZE) {
        virtio_mmio_write(&vm->virtio_blk.vdev,
                          addr - SV_ADDR_VIRTIO_BLK, value, size);
        return;
    }

    if (vm->virtio_console_enabled &&
        addr >= SV_ADDR_VIRTIO_CONSOLE &&
        addr < SV_ADDR_VIRTIO_CONSOLE + SV_ADDR_VIRTIO_SIZE) {
        virtio_mmio_write(&vm->virtio_console.vdev,
                          addr - SV_ADDR_VIRTIO_CONSOLE, value, size);
        return;
    }

    if (vm->virtio_net_enabled &&
        addr >= SV_ADDR_VIRTIO_NET &&
        addr < SV_ADDR_VIRTIO_NET + SV_ADDR_VIRTIO_SIZE) {
        virtio_mmio_write(&vm->virtio_net.vdev,
                          addr - SV_ADDR_VIRTIO_NET, value, size);
        return;
    }

    fprintf(stderr, "sv_apple: unmapped MMIO write at 0x%lx = 0x%lx (size=%d)\n",
            (unsigned long)addr, (unsigned long)value, size);
}

/* ── vCPU boot (XNU protocol: x0 = BootArgs addr) ─ */
#define CPSR_EL1h_DAIF_MASKED 0x3C5

static int apple_setup_vcpu(const sv_hv_ops_t *hv, sv_vcpu_t *vcpu,
                             uint64_t kernel_entry_phys, uint64_t bootargs_addr,
                             uint64_t ram_base, uint64_t ram_size,
                             int cpu_id)
{
    (void)cpu_id;
    if (hv->vcpu_set_reg(vcpu, 0, bootargs_addr) < 0)  /* x0 = BootArgs (PHYSICAL addr) */
        return -1;
    if (hv->vcpu_set_reg(vcpu, 1, 0) < 0)               /* x1 = 0 */
        return -1;
    if (hv->vcpu_set_reg(vcpu, 2, 0) < 0)               /* x2 = 0 */
        return -1;
    /* Set SP to top of guest RAM */
    if (hv->vcpu_set_reg(vcpu, 31, ram_base + ram_size) < 0)  /* SP (reg 31) */
        return -1;
    /* Set LR (x30) to kernel_entry */
    if (hv->vcpu_set_reg(vcpu, 30, kernel_entry_phys) < 0)   /* LR = physical entry */
        return -1;
    /* Set PSTATE — EL1h with all exceptions masked */
    if (hv->vcpu_set_reg(vcpu, 33, CPSR_EL1h_DAIF_MASKED) < 0)  /* PSTATE (reg 33) */
        return -1;

    /*
     * Note: PC is NOT set here. It will be set by tcg_vcpu_enable_mmu()
     * to the VIRTUAL entry point when using the TCG backend.
     * For KVM/HVF backends, the hardware handles MMU natively.
     */
    return 0;
}

/* ── Main Loop ──────────────────────────────────── */
int sv_machine_apple_run(sv_machine_apple_t *vm)
{
    if (!vm->kernel_entry) {
        fprintf(stderr, "sv_apple: no kernel loaded\n");
        return -1;
    }

    uint64_t bootargs_addr = xnu_boot_get_bootargs_addr(&vm->xnu_boot);
    uint64_t kernel_virt_entry = xnu_boot_get_virt_entry(&vm->xnu_boot);
    uint64_t kernel_virt_base = xnu_boot_get_virt_base(&vm->xnu_boot);

    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  SiliconV — Apple VM Starting\n");
    printf("  Kernel PA: 0x%lx\n", (unsigned long)vm->kernel_entry);
    printf("  Kernel VA: 0x%lx\n", (unsigned long)kernel_virt_entry);
    printf("  BootArgs:  0x%lx\n", (unsigned long)bootargs_addr);
    printf("  CPUs:      %d\n", vm->num_cpus);
    printf("  RAM:       %lu MB\n",
           (unsigned long)(vm->ram_size / (1024 * 1024)));
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\n");

    /* ── Set up identity map for MMU (TCG backend) ─────── */
    tcg_mmu_set_identity_map(
        kernel_virt_base,
        vm->ram_base + vm->xnu_boot.boot_info.aslr_slide, /* kernel phys base */
        vm->ram_base,
        vm->ram_size);

    /* Check for hypervisor backend */
    const sv_hv_ops_t *hv = sv_hv_get_best();
    if (!hv) {
        vm->running = true;
        printf("sv_apple: [placeholder] no hypervisor backend available\n");
        printf("sv_apple: vCPU would execute at 0x%lx with BootArgs at 0x%lx\n",
               (unsigned long)vm->kernel_entry,
               (unsigned long)bootargs_addr);
        vm->running = false;
        return 0;
    }

    printf("sv_apple: using backend '%s'\n", hv->name);

    /* ── Create VM ──────────────────────────────── */
    sv_vm_config_t cfg = sv_vm_config_default();
    cfg.num_cpus = vm->num_cpus;
    cfg.ram_size = vm->ram_size;
    cfg.ram_base = vm->ram_base;
    cfg.preallocated_ram = vm->ram;
    cfg.kernel_entry = vm->kernel_entry;
    cfg.dtb_addr = bootargs_addr; /* Reuse dtb_addr field for BootArgs */
    cfg.cmdline = NULL;

    cfg.mmio_read = machine_apple_mmio_read;
    cfg.mmio_write = machine_apple_mmio_write;
    cfg.callback_opaque = vm;

    sv_vm_t *hvm = hv->vm_create(&cfg);
    if (!hvm) {
        fprintf(stderr, "sv_apple: VM creation failed\n");
        return -1;
    }

    /* Register MMIO regions */
    if (hv->mmio_register) {
        sv_mmio_handler_t dummy = {NULL, NULL, NULL};
        hv->mmio_register(hvm, SV_APPLE_AIC_BASE, SV_APPLE_AIC_SIZE, &dummy);
        hv->mmio_register(hvm, SV_APPLE_UART0_BASE, SV_APPLE_UART0_SIZE, &dummy);
        hv->mmio_register(hvm, SV_APPLE_DART0_BASE, SV_APPLE_DART0_SIZE, &dummy);
        hv->mmio_register(hvm, SV_APPLE_DART1_BASE, SV_APPLE_DART1_SIZE, &dummy);
        hv->mmio_register(hvm, SV_APPLE_SEP_BASE, SV_APPLE_SEP_SIZE, &dummy);
        hv->mmio_register(hvm, SV_APPLE_WDT_BASE, SV_APPLE_WDT_SIZE, &dummy);
        hv->mmio_register(hvm, SV_APPLE_NVRAM_BASE, SV_APPLE_NVRAM_SIZE, &dummy);
    }

    /* Load kernel/bootargs into backend RAM */
    if (hv->load_kernel)
        hv->load_kernel(hvm, NULL);
    if (hv->load_dtb)
        hv->load_dtb(hvm, NULL);

    /* ── Create vCPUs ────────────────────────────── */
    sv_vcpu_t *vcpus[8];
    int num_vcpus = (vm->num_cpus < 8) ? vm->num_cpus : 8;

    for (int i = 0; i < num_vcpus; i++) {
        vcpus[i] = hv->vcpu_create(hvm, i);
        if (!vcpus[i]) {
            fprintf(stderr, "sv_apple: vCPU %d creation failed\n", i);
            hv->vm_destroy(hvm);
            return -1;
        }

        if (apple_setup_vcpu(hv, vcpus[i],
                              vm->kernel_entry, bootargs_addr,
                              vm->ram_base, vm->ram_size, i) < 0) {
            fprintf(stderr, "sv_apple: vCPU %d setup failed\n", i);
            hv->vm_destroy(hvm);
            return -1;
        }

        /* Enable MMU for TCG backend (sets virtual PC, MMU registers) */
        if (hv->type == SV_HV_TCG) {
            tcg_vcpu_enable_mmu(vcpus[i], kernel_virt_entry,
                                 vm->ram_base, vm->ram_size);
        }
    }

    /* ── Run loop ────────────────────────────────── */
    vm->running = true;
    sv_vcpu_exit_t exit_info;
    int ret = 0;

    printf("sv_apple: entering main loop (vCPU 0)\n\n");

    while (vm->running) {
        ret = hv->vcpu_run(vcpus[0], &exit_info);
        if (ret < 0) {
            fprintf(stderr, "sv_apple: vCPU run error\n");
            break;
        }

        switch (exit_info.type) {
        case SV_EXIT_MMIO_READ:
        case SV_EXIT_MMIO_WRITE:
            break; /* Handled by backend callbacks */

        case SV_EXIT_HLT:
            printf("sv_apple: guest HLT\n");
            vm->running = false;
            break;

        case SV_EXIT_SHUTDOWN:
            printf("sv_apple: guest shutdown\n");
            vm->running = false;
            break;

        case SV_EXIT_UNKNOWN:
            break;
        }
    }

    hv->vm_destroy(hvm);
    printf("sv_apple: VM stopped\n");
    return ret;
}

void sv_machine_apple_stop(sv_machine_apple_t *vm)
{
    vm->running = false;
    printf("sv_apple: VM stopped\n");
}
