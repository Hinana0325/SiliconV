/*
 * SiliconV Launcher — Main Entry Point
 *
 * Supports two platform profiles:
 *   --platform android  (default) — Linux/Android kernels with GICv3, PL011, etc.
 *   --platform apple    (new)     — XNU kernels with AIC, Apple UART, DART, SEP, etc.
 *
 * Both profiles share the same hypervisor backends (KVM/HVF/TCG).
 */

#include "../../core/vm/machine.h"
#include "../../core/vm/machine_apple.h"
#include "../../core/boot/apple/xnu_boot.h"
#include "../../devices/apple-aic/apple_aic.h"
#include "../../devices/apple-uart/apple_uart.h"
#include "../../devices/apple-sep/apple_sep.h"
#include "../../core/irq/gic.h"
#include "../../core/memory/mmio_addrs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>

static volatile int running = 1;
static sv_machine_t *g_vm = NULL;
static sv_machine_apple_t *g_vm_apple = NULL;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
    if (g_vm)
        sv_machine_stop(g_vm);
    if (g_vm_apple)
        sv_machine_apple_stop(g_vm_apple);
}

static int parse_positive_int(const char *text, const char *name,
                              int min_value, int max_value)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (errno != 0 || !end || *end != '\0' ||
        value < min_value || value > max_value) {
        fprintf(stderr, "sv: invalid %s '%s' (expected %d-%d)\n",
                name, text, min_value, max_value);
        return -1;
    }

    return (int)value;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "SiliconV v0.1 — Virtual Phone Hardware Platform\n"
        "\n"
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Platform Selection:\n"
        "      --platform TYPE   Platform profile: android (default) or apple\n"
        "\n"
        "Android Profile Options:\n"
        "  -k, --kernel PATH    Kernel image or Android boot.img\n"
        "  -d, --dtb PATH       Device tree blob (optional, generated if omitted)\n"
        "  -r, --rootfs PATH    Root filesystem image for virtio-blk\n"
        "  -c, --cmdline STR    Kernel command line\n"
        "\n"
        "Apple Profile Options:\n"
        "  -k, --kernel PATH    XNU kernel (IMG4 container or raw Mach-O)\n"
        "  -d, --dtb PATH       Apple DeviceTree blob (optional 'dtre' payload)\n"
        "  -r, --rootfs PATH    Root filesystem image for virtio-blk\n"
        "  -c, --cmdline STR    Boot-args string (optional)\n"
        "\n"
        "General Options:\n"
        "  -m, --memory SIZE    Guest RAM in MB (default: 4096, min: 64)\n"
        "  -n, --cpus NUM       Number of vCPUs (default: 4, max: 8)\n"
        "      --dry-run        Load and validate without starting vCPUs\n"
        "      --list-devices   List all available devices for both profiles\n"
        "  -h, --help           Show this help\n",
        prog);
}

static int run_android(const char *kernel_path, const char *dtb_path,
                        const char *rootfs_path, const char *cmdline,
                        int num_cpus, uint64_t ram_size, int dry_run)
{
    sv_machine_t vm;
    g_vm = &vm;

    if (sv_machine_init(&vm, num_cpus, ram_size) < 0)
        return 1;

    if (sv_machine_load_kernel(&vm, kernel_path) < 0) {
        sv_machine_destroy(&vm);
        return 1;
    }

    if (dtb_path) {
        if (sv_machine_load_dtb(&vm, dtb_path) < 0) {
            sv_machine_destroy(&vm);
            return 1;
        }
    }

    if (rootfs_path) {
        if (sv_machine_attach_virtio_blk(&vm, rootfs_path, false) < 0) {
            fprintf(stderr, "sv: failed to attach rootfs\n");
            sv_machine_destroy(&vm);
            return 1;
        }
    }

    if (sv_machine_attach_virtio_net(&vm) < 0) {
        fprintf(stderr, "sv: warning: failed to attach virtio-net\n");
    }

    if (sv_machine_attach_virtio_console(&vm) < 0) {
        fprintf(stderr, "sv: warning: failed to attach virtio-console\n");
    }

    if (cmdline) {
        free((void *)vm.dtb_config.cmdline);
        vm.dtb_config.cmdline = strdup(cmdline);
    }

    if (!vm.dtb_addr && sv_machine_generate_dtb(&vm) < 0) {
        sv_machine_destroy(&vm);
        return 1;
    }

    if (dry_run) {
        printf("sv: dry run complete — configuration is loadable\n");
        sv_machine_destroy(&vm);
        g_vm = NULL;
        return 0;
    }

    int ret = sv_machine_run(&vm);
    sv_machine_destroy(&vm);
    g_vm = NULL;
    return ret;
}

static int run_apple(const char *kernel_path, const char *dtb_path,
                      const char *rootfs_path, const char *cmdline,
                      int num_cpus, uint64_t ram_size, int dry_run)
{
    sv_machine_apple_t vm;
    g_vm_apple = &vm;

    if (sv_machine_apple_init(&vm, num_cpus, ram_size) < 0)
        return 1;

    /* Read kernel file into memory */
    FILE *f = fopen(kernel_path, "rb");
    if (!f) {
        fprintf(stderr, "sv_apple: cannot open kernel: %s\n", kernel_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long ksize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *kdata = malloc(ksize);
    if (!kdata) {
        fclose(f);
        fprintf(stderr, "sv_apple: out of memory\n");
        return 1;
    }
    size_t nread = fread(kdata, 1, ksize, f);
    fclose(f);

    if ((long)nread != ksize) {
        fprintf(stderr, "sv_apple: short read on kernel\n");
        free(kdata);
        return 1;
    }

    if (sv_machine_apple_load_kernel(&vm, kdata, ksize, cmdline) < 0) {
        free(kdata);
        sv_machine_apple_destroy(&vm);
        return 1;
    }

    /* Load DTB if provided */
    if (dtb_path) {
        f = fopen(dtb_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long dsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            uint8_t *ddata = malloc(dsize);
            if (ddata) {
                fread(ddata, 1, dsize, f);
                sv_machine_apple_load_dtb(&vm, ddata, dsize);
                free(ddata);
            }
            fclose(f);
        }
    }

    /* Attach root filesystem (virtio-blk) */
    if (rootfs_path) {
        if (sv_machine_apple_attach_virtio_blk(&vm, rootfs_path, false) < 0) {
            fprintf(stderr, "sv_apple: failed to attach rootfs\n");
            sv_machine_apple_destroy(&vm);
            return 1;
        }
    }

    /* Attach virtio devices */
    if (sv_machine_apple_attach_virtio_net(&vm) < 0) {
        fprintf(stderr, "sv_apple: warning: failed to attach virtio-net\n");
    }
    if (sv_machine_apple_attach_virtio_console(&vm) < 0) {
        fprintf(stderr, "sv_apple: warning: failed to attach virtio-console\n");
    }

    free(kdata);

    if (dry_run) {
        printf("sv_apple: dry run complete — configuration is loadable\n");
        sv_machine_apple_destroy(&vm);
        g_vm_apple = NULL;
        return 0;
    }

    int ret = sv_machine_apple_run(&vm);
    sv_machine_apple_destroy(&vm);
    g_vm_apple = NULL;
    return ret;
}

int main(int argc, char *argv[])
{
    const char *kernel_path = NULL;
    const char *dtb_path = NULL;
    const char *rootfs_path = NULL;
    const char *cmdline = NULL;
    const char *platform = "android";
    int num_cpus = 4;
    int ram_mb = 4096;
    int dry_run = 0;

    static struct option long_opts[] = {
        {"kernel",  required_argument, 0, 'k'},
        {"dtb",     required_argument, 0, 'd'},
        {"rootfs",  required_argument, 0, 'r'},
        {"cmdline", required_argument, 0, 'c'},
        {"memory",  required_argument, 0, 'm'},
        {"cpus",    required_argument, 0, 'n'},
        {"dry-run", no_argument,       0, 1000},
        {"platform",required_argument, 0, 1001},
        {"list-devices", no_argument,  0, 1002},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "k:d:r:c:m:n:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'k': kernel_path = optarg; break;
        case 'd': dtb_path = optarg; break;
        case 'r': rootfs_path = optarg; break;
        case 'c': cmdline = optarg; break;
        case 'm':
            ram_mb = parse_positive_int(optarg, "memory size", 64, 1024 * 1024);
            if (ram_mb < 0) return 1;
            break;
        case 'n':
            num_cpus = parse_positive_int(optarg, "CPU count", 1, 8);
            if (num_cpus < 0) return 1;
            break;
        case 1000: dry_run = 1; break;
        case 1001: platform = optarg; break;
        case 1002: {
            printf("SiliconV — Device Listing\n\n");
            printf("Android Profile Devices:\n");
            printf("  GICv3      0x%08lx  IRQ %d-%d\n",
                   (unsigned long)SV_ADDR_GICD_BASE, 0, (int)GIC_MAX_IRQ-1);
            printf("  UART(PL011) 0x%08lx  IRQ %d\n",
                   (unsigned long)SV_ADDR_UART0_BASE, SV_IRQ_UART0);
            printf("  Virtio-BLK 0x%08lx  IRQ %d\n",
                   (unsigned long)SV_ADDR_VIRTIO_BLK, SV_IRQ_VIRTIO_BLK);
            printf("  Virtio-NET 0x%08lx  IRQ %d\n",
                   (unsigned long)SV_ADDR_VIRTIO_NET, SV_IRQ_VIRTIO_NET);
            printf("  Virtio-CON 0x%08lx  IRQ %d\n\n",
                   (unsigned long)SV_ADDR_VIRTIO_CONSOLE, SV_IRQ_VIRTIO_CONSOLE);
            printf("Apple Profile Devices:\n");
            printf("  AIC        0x%08lx  IRQ 32-575\n",
                   (unsigned long)SV_APPLE_AIC_BASE);
            printf("  UART(S5L)  0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_UART0_BASE, SV_IRQ_APPLE_UART0);
            printf("  DART-0     0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_DART0_BASE, SV_IRQ_APPLE_DART0);
            printf("  DART-1     0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_DART1_BASE, SV_IRQ_APPLE_DART1);
            printf("  SEP        0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_SEP_BASE, SV_IRQ_APPLE_SEP);
            printf("  WDT        0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_WDT_BASE, SV_IRQ_APPLE_WDT);
            printf("  NVRAM      0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_NVRAM_BASE, SV_IRQ_APPLE_NVRAM);
            printf("  Timer      0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_TIMER_BASE, SV_IRQ_APPLE_TIMER);
            printf("  GPIO       0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_GPIO_BASE, SV_IRQ_APPLE_GPIO);
            printf("  I2C        0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_I2C0_BASE, SV_IRQ_APPLE_I2C0);
            printf("  SPMI       0x%08lx  IRQ %d\n",
                   (unsigned long)SV_APPLE_SPMI_BASE, SV_IRQ_APPLE_SPMI);
            printf("  Virtio-BLK 0x%08lx  IRQ %d\n",
                   (unsigned long)SV_ADDR_VIRTIO_BLK, SV_IRQ_APPLE_VIRTIO_BLK0);
            printf("  RAM (Apple) 0x%llx  (%llu MB)\n",
                   (unsigned long long)SV_APPLE_RAM_BASE,
                   (unsigned long long)(SV_APPLE_RAM_DEFAULT / (1024*1024)));
            return 0;
        }
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (!kernel_path) {
        fprintf(stderr, "sv: kernel image required (-k)\n");
        print_usage(argv[0]);
        return 1;
    }

    signal(SIGINT, sigint_handler);

    printf("SiliconV v0.1 — Platform: %s\n\n", platform);

    uint64_t ram_size = (uint64_t)ram_mb * 1024 * 1024;

    if (strcmp(platform, "apple") == 0) {
        /* ── Apple (XNU) Profile ───────────────── */
        if (!cmdline)
            cmdline = "debug=0x8 serial=2 -v keepsyms=1";
        return run_apple(kernel_path, dtb_path, rootfs_path, cmdline,
                         num_cpus, ram_size, dry_run);
    } else {
        /* ── Android (Linux) Profile (default) ─── */
        if (!cmdline)
            cmdline = "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw";
        return run_android(kernel_path, dtb_path, rootfs_path, cmdline,
                           num_cpus, ram_size, dry_run);
    }
}
