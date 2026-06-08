/*
 * SiliconV Launcher — Main Entry Point (Updated)
 *
 * Uses the machine module to tie everything together.
 * Supports both raw kernel and Android boot.img.
 */

#include "../../core/vm/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static volatile int running = 1;
static sv_machine_t *g_vm = NULL;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
    if (g_vm)
        sv_machine_stop(g_vm);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "SiliconV v0.1 — Virtual Phone Hardware Platform\n"
        "\n"
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -k, --kernel PATH    Kernel image or Android boot.img (required)\n"
        "  -d, --dtb PATH       Device tree blob (optional, generated if omitted)\n"
        "  -r, --rootfs PATH    Root filesystem image for virtio-blk\n"
        "  -c, --cmdline STR    Kernel command line\n"
        "  -m, --memory SIZE    Guest RAM in MB (default: 4096)\n"
        "  -n, --cpus NUM       Number of vCPUs (default: 4)\n"
        "  -h, --help           Show this help\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *kernel_path = NULL;
    const char *dtb_path = NULL;
    const char *rootfs_path = NULL;
    const char *cmdline = "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw";
    int num_cpus = 4;
    int ram_mb = 4096;

    static struct option long_opts[] = {
        {"kernel",  required_argument, 0, 'k'},
        {"dtb",     required_argument, 0, 'd'},
        {"rootfs",  required_argument, 0, 'r'},
        {"cmdline", required_argument, 0, 'c'},
        {"memory",  required_argument, 0, 'm'},
        {"cpus",    required_argument, 0, 'n'},
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
        case 'm': ram_mb = atoi(optarg); break;
        case 'n': num_cpus = atoi(optarg); break;
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

    printf("SiliconV v0.1\n\n");

    /* Initialize machine */
    sv_machine_t vm;
    g_vm = &vm;

    uint64_t ram_size = (uint64_t)ram_mb * 1024 * 1024;
    if (sv_machine_init(&vm, num_cpus, ram_size) < 0)
        return 1;

    /* Load kernel (supports Android boot.img and raw binary) */
    if (sv_machine_load_kernel(&vm, kernel_path) < 0) {
        sv_machine_destroy(&vm);
        return 1;
    }

    /* Load or generate DTB */
    if (dtb_path) {
        if (sv_machine_load_dtb(&vm, dtb_path) < 0) {
            sv_machine_destroy(&vm);
            return 1;
        }
    }

    /* Attach root filesystem */
    if (rootfs_path) {
        if (sv_machine_attach_virtio_blk(&vm, rootfs_path, false) < 0) {
            fprintf(stderr, "sv: failed to attach rootfs\n");
            sv_machine_destroy(&vm);
            return 1;
        }
    }

    /* Attach network device */
    if (sv_machine_attach_virtio_net(&vm) < 0) {
        fprintf(stderr, "sv: warning: failed to attach virtio-net\n");
    }

    /* Attach console device */
    if (sv_machine_attach_virtio_console(&vm) < 0) {
        fprintf(stderr, "sv: warning: failed to attach virtio-console\n");
    }

    /* Update cmdline if provided */
    if (cmdline)
        vm.dtb_config.cmdline = cmdline;

    /* Run the VM */
    int ret = sv_machine_run(&vm);

    sv_machine_destroy(&vm);
    g_vm = NULL;

    return ret;
}
