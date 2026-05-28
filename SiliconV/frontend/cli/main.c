/*
 * SiliconV Launcher — Main Entry Point
 *
 * Creates and runs a SiliconV virtual machine.
 * This is the CLI frontend that ties everything together.
 */

#include "../../hypervisor/abstraction/hv.h"
#include "../../devices/uart/pl011.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static volatile int running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* UART TX callback — print guest output to host terminal */
static void uart_tx_output(uint8_t byte, void *opaque)
{
    (void)opaque;
    putchar(byte);
    fflush(stdout);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "SiliconV — Virtual Phone Hardware Platform\n"
        "\n"
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -k, --kernel PATH    Kernel image (required for direct boot)\n"
        "  -d, --dtb PATH       Device tree blob (optional)\n"
        "  -i, --initrd PATH    Initial ramdisk (optional)\n"
        "  -c, --cmdline STR    Kernel command line\n"
        "  -m, --memory SIZE    Guest RAM in MB (default: 4096)\n"
        "  -n, --cpus NUM       Number of vCPUs (default: 4)\n"
        "  -h, --help           Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    sv_vm_config_t config = sv_vm_config_default();
    const char *kernel_path = NULL;
    const char *dtb_path = NULL;
    const char *initrd_path = NULL;

    static struct option long_opts[] = {
        {"kernel",  required_argument, 0, 'k'},
        {"dtb",     required_argument, 0, 'd'},
        {"initrd",  required_argument, 0, 'i'},
        {"cmdline", required_argument, 0, 'c'},
        {"memory",  required_argument, 0, 'm'},
        {"cpus",    required_argument, 0, 'n'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "k:d:i:c:m:n:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'k': kernel_path = optarg; break;
        case 'd': dtb_path = optarg; break;
        case 'i': initrd_path = optarg; break;
        case 'c': config.cmdline = optarg; break;
        case 'm': config.ram_size = (uint64_t)atoi(optarg) * 1024 * 1024; break;
        case 'n': config.num_cpus = atoi(optarg); break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (!kernel_path) {
        fprintf(stderr, "sv: kernel image required (-k)\n");
        print_usage(argv[0]);
        return 1;
    }

    config.kernel_path = kernel_path;
    config.dtb_path = dtb_path;
    config.initrd_path = initrd_path;

    /* Setup signal handler */
    signal(SIGINT, sigint_handler);

    printf("SiliconV v0.1\n");
    printf("  CPUs: %d\n", config.num_cpus);
    printf("  RAM:  %lu MB\n", config.ram_size / (1024 * 1024));
    printf("  Kernel: %s\n", kernel_path);
    if (dtb_path) printf("  DTB: %s\n", dtb_path);
    printf("\n");

    /* Initialize hypervisor */
    const sv_hv_ops_t *hv = sv_hv_get_best();
    if (!hv) {
        fprintf(stderr, "sv: no hypervisor available\n");
        return 1;
    }

    printf("sv: using %s backend\n", hv->name);

    if (hv->init() < 0) {
        fprintf(stderr, "sv: hypervisor init failed\n");
        return 1;
    }

    /* Create VM */
    sv_vm_t *vm = hv->vm_create(&config);
    if (!vm) {
        fprintf(stderr, "sv: VM creation failed\n");
        return 1;
    }

    /* Load kernel and DTB */
    if (hv->load_kernel(vm, kernel_path) < 0) {
        hv->vm_destroy(vm);
        return 1;
    }

    if (dtb_path && hv->load_dtb) {
        if (hv->load_dtb(vm, dtb_path) < 0) {
            hv->vm_destroy(vm);
            return 1;
        }
    }

    /* Create vCPUs */
    for (int i = 0; i < config.num_cpus; i++) {
        sv_vcpu_t *vcpu = hv->vcpu_create(vm, i);
        if (!vcpu) {
            fprintf(stderr, "sv: failed to create vCPU %d\n", i);
            hv->vm_destroy(vm);
            return 1;
        }
    }

    printf("sv: VM created, starting execution...\n\n");

    /* Main loop — simplified for v0 */
    sv_vcpu_exit_t exit;
    /* TODO: implement full vCPU run loop with MMIO dispatch */

    printf("\nsv: shutdown\n");
    hv->vm_destroy(vm);
    return 0;
}
