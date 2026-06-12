/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SiliconV Kernel Boot Verification - QEMU Initramfs init
 *
 * Compiled as a static ARM64 binary and packed into an initramfs cpio
 * archive.  The kernel executes this as /init, which validates that
 * Android-required kernel features are available, then powers off.
 *
 * Build:
 *   aarch64-linux-gnu-gcc -static -Os -o init test_kernel_boot_qemu.c
 *
 * Expected devices (kernel config):
 *   - CONFIG_ANDROID_BINDER_IPC -> /dev/{binder,hwbinder,vndbinder}
 *   - CONFIG_DMABUF_HEAPS       -> /dev/dma_heap + /sys/kernel/dmabuf/heaps
 *   - CONFIG_VIRTIO_*           -> /sys/bus/virtio/devices
 *   - CONFIG_USERFAULTFD        -> /dev/userfaultfd
 *   - CONFIG_SERIAL_AMBA_PL011  -> ttyAMA0 console
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <dirent.h>
#include <errno.h>

/* Maximum number of test checks */
#define MAX_CHECKS 32

static int total_checks = 0;
static int pass_checks = 0;
static int fail_checks = 0;

/* Mode flags for check entries */
#define M_CHR   (1 << 0)  /* character device */
#define M_BLK   (1 << 1)  /* block device */
#define M_DIR   (1 << 2)  /* directory */
#define M_ANY   (M_CHR | M_BLK | M_DIR)

typedef struct {
    const char *name;
    const char *path;
    int  mode_flags;   /* bitmask of acceptable modes (M_CHR, M_BLK, M_DIR) */
} check_entry;

static check_entry checks[] = {
    /* Android binder devices (character devices) */
    {"binder",     "/dev/binder",      M_CHR},
    {"hwbinder",   "/dev/hwbinder",    M_CHR},
    {"vndbinder",  "/dev/vndbinder",   M_CHR},

    /* dma-buf heaps (directory in devtmpfs) */
    {"dma_heap",   "/dev/dma_heap",    M_DIR | M_CHR},

    /* userfaultfd (character device) */
    {"userfaultfd","/dev/userfaultfd", M_CHR},

    /* UART console (character device) */
    {"ttyAMA0",    "/dev/ttyAMA0",     M_CHR},

    /* Helper device nodes for container/VM operation */
    {"loop0",      "/dev/loop0",       M_BLK},
    {"fuse",       "/dev/fuse",        M_CHR},

    /* Virtio bus (sysfs directory) */
    {"virtio",     "/sys/bus/virtio/devices", M_DIR},
};

static const char *mode_str(mode_t mode)
{
    if (S_ISCHR(mode)) return "chr";
    if (S_ISBLK(mode)) return "blk";
    if (S_ISDIR(mode)) return "dir";
    if (S_ISREG(mode)) return "reg";
    if (S_ISLNK(mode)) return "lnk";
    if (S_ISFIFO(mode)) return "fifo";
    if (S_ISSOCK(mode)) return "sock";
    return "?";
}

static void check_device(check_entry *e)
{
    struct stat st;
    int rc = stat(e->path, &st);

    total_checks++;
    printf("  %-12s  %-30s  ", e->name, e->path);

    int ok = 0;
    if (rc == 0) {
        int mode_match = 0;
        if ((e->mode_flags & M_CHR) && S_ISCHR(st.st_mode))
            mode_match = 1;
        if ((e->mode_flags & M_BLK) && S_ISBLK(st.st_mode))
            mode_match = 1;
        if ((e->mode_flags & M_DIR) && S_ISDIR(st.st_mode))
            mode_match = 1;
        ok = mode_match;
    }

    if (ok) {
        printf("PASS  (%s)\n", mode_str(st.st_mode));
        pass_checks++;
    } else {
        if (rc == 0)
            printf("FAIL  (found %s, expected mode mask 0x%x)\n",
                   mode_str(st.st_mode), e->mode_flags);
        else
            printf("FAIL  (rc=%d, errno=%d)\n", rc, errno);
        fail_checks++;
    }
}

static void list_virtio_devices(void)
{
    DIR *d = opendir("/sys/bus/virtio/devices");
    if (!d) {
        printf("  No virtio devices sysfs directory\n");
        return;
    }

    struct dirent *de;
    int count = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        if (count == 0)
            printf("  virtio devices:\n");
        printf("    %s\n", de->d_name);
        count++;
    }
    closedir(d);

    if (count == 0)
        printf("  No virtio devices found\n");
}

static void print_header(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════╗\n");
    printf("  ║   SiliconV Kernel v6.6 Boot Verification    ║\n");
    printf("  ╚══════════════════════════════════════════════╝\n");
    printf("\n");
}

static void print_footer(void)
{
    printf("\n");
    printf("  ──────────────────────────────────────────────\n");
    printf("  Results:  %d/%d passed", pass_checks, total_checks);
    if (fail_checks > 0)
        printf(",  %d FAILED", fail_checks);
    printf("\n");
}

int main(int argc, char *argv[])
{
    /* Mount essential virtual filesystems */
    mount("none", "/proc", "proc",   0, NULL);
    mount("none", "/sys",  "sysfs",  0, NULL);
    mount("none", "/dev",  "devtmpfs", 0, NULL);

    print_header();

    /* Read kernel version */
    FILE *f = fopen("/proc/version", "r");
    if (f) {
        char buf[256];
        if (fgets(buf, sizeof(buf), f))
            printf("  Kernel: %s", buf);
        fclose(f);
    }

    /* Count CPUs */
    int cpus = 0;
    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f))
            if (strstr(buf, "processor"))
                cpus++;
        fclose(f);
    }
    printf("  CPUs:   %d\n", cpus ? cpus : 1);

    /* Memory info */
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            if (strstr(buf, "MemTotal")) {
                buf[strcspn(buf, "\n")] = '\0';
                printf("  Memory: %s\n", buf);
                break;
            }
        }
        fclose(f);
    }

    printf("\n  ─── Device Check ───\n");

    size_t i;
    for (i = 0; i < sizeof(checks)/sizeof(checks[0]); i++)
        check_device(&checks[i]);

    printf("\n  ─── Virtio Devices ───\n");
    list_virtio_devices();

    print_footer();

    /* Flush output before poweroff */
    fflush(stdout);
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);

    /* Should never reach here */
    return 0;
}
