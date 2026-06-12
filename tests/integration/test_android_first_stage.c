/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SiliconV Android FirstStage Init (Fallback)
 *
 * A static ARM64 binary that serves as /init for System-as-Root boot
 * when GSI's own ramdisk is not available. It performs the minimum
 * first-stage setup and then hands off to AOSP's /system/bin/init.
 *
 * Build:
 *   aarch64-linux-gnu-gcc -static -Os -o init test_android_first_stage.c
 *
 * Boot flow:
 *   1. Mount /proc, /sys, /dev (devtmpfs)
 *   2. Wait for /dev/vda (virtio-blk system partition)
 *   3. Mount /dev/vda -> /system (ext4, ro)
 *   4. Mount /dev/vdb -> /vendor (ext4, ro)
 *   5. Import androidboot.* properties from kernel cmdline
 *   6. execv("/system/bin/init", "selinux_setup") -> AOSP takes over
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define LOG_TAG "siliconv-firststage"

/* Maximum wait time for virtio-blk device to appear (seconds) */
#define VDA_WAIT_TIMEOUT 30

/* Property service socket / buffer paths used by AOSP init */
#define PROP_SERVICE_SOCKET "/dev/__properties__"
#define PROP_AREA_SIZE      (128 * 1024)

/*
 * Write a message to the kernel log (visible via dmesg / logcat).
 * Uses kmsg if available, falls back to stdout.
 */
static void klog(const char *msg)
{
    static int kmsg_fd = -1;

    if (kmsg_fd < 0)
        kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

    if (kmsg_fd >= 0) {
        char buf[512];
        int len = snprintf(buf, sizeof(buf), "%s: %s\n", LOG_TAG, msg);
        write(kmsg_fd, buf, (size_t)len);
    } else {
        printf("%s: %s\n", LOG_TAG, msg);
        fflush(stdout);
    }
}

/*
 * Wait for a block device file to appear with timeout.
 * Returns 0 on success, -1 on timeout.
 */
static int wait_for_device(const char *path, int timeout_sec)
{
    struct stat st;
    int waited = 0;

    while (stat(path, &st) != 0 || !S_ISBLK(st.st_mode)) {
        if (waited >= timeout_sec) {
            return -1;
        }
        usleep(500 * 1000);  /* 500ms */
        waited++;
    }
    return 0;
}

/*
 * Parse the kernel command line and import androidboot.* properties.
 * In AOSP, this is done by the property service; here we create a
 * minimal /dev/__properties__ area and set the properties so that
 * /system/bin/init can read them.
 *
 * For a full GSI boot, the property area format must match what
 * AOSP's __system_property_area_init expects.  As a fallback, we
 * write the androidboot.* values to /proc/cmdline for the next init
 * stage to re-parse.
 */
static void import_androidboot_properties(void)
{
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f) {
        klog("WARNING: cannot read /proc/cmdline");
        return;
    }

    char cmdline[2048];
    if (!fgets(cmdline, sizeof(cmdline), f)) {
        fclose(f);
        return;
    }
    fclose(f);

    /* Create /dev/__properties__ directory for property area */
    mkdir("/dev/__properties__", 0755);

    /*
     * Write each androidboot.* parameter to a file under
     * /dev/__properties__/ so that /system/bin/init can import them.
     * The actual AOSP property service initialisation is done by
     * selinux_setup, but we ensure the cmdline is accessible.
     */
    char *saveptr = NULL;
    char cmdline_copy[sizeof(cmdline)];
    strncpy(cmdline_copy, cmdline, sizeof(cmdline_copy) - 1);
    cmdline_copy[sizeof(cmdline_copy) - 1] = '\0';

    char *token = strtok_r(cmdline_copy, " \t\n", &saveptr);
    while (token != NULL) {
        if (strncmp(token, "androidboot.", 12) == 0) {
            /* Extract key=value */
            char *eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                const char *key = token + 12;  /* skip "androidboot." */
                const char *value = eq + 1;

                /* Write to a simple key-value file */
                char prop_path[256];
                snprintf(prop_path, sizeof(prop_path),
                         "/dev/__properties__/androidboot.%s", key);

                FILE *pf = fopen(prop_path, "w");
                if (pf) {
                    fprintf(pf, "%s", value);
                    fclose(pf);
                    chmod(prop_path, 0444);
                }
            }
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    klog("Imported androidboot.* properties from kernel cmdline");
}

int main(int argc, char *argv[])
{
    char msg[256];

    klog("SiliconV FirstStage init starting");

    /* ── Step 1: Mount essential virtual filesystems ─────────── */

    if (mount("none", "/proc", "proc", 0, NULL) != 0) {
        snprintf(msg, sizeof(msg), "Failed to mount /proc: %s", strerror(errno));
        klog(msg);
    } else {
        klog("Mounted /proc");
    }

    if (mount("none", "/sys", "sysfs", 0, NULL) != 0) {
        snprintf(msg, sizeof(msg), "Failed to mount /sys: %s", strerror(errno));
        klog(msg);
    } else {
        klog("Mounted /sys");
    }

    if (mount("none", "/dev", "devtmpfs", 0, NULL) != 0) {
        snprintf(msg, sizeof(msg), "Failed to mount /dev: %s", strerror(errno));
        klog(msg);
    } else {
        klog("Mounted /dev (devtmpfs)");
    }

    mkdir("/dev/pts", 0755);
    mkdir("/dev/socket", 0755);

    /* ── Step 2: Wait for /dev/vda (virtio-blk system) ──────── */

    klog("Waiting for /dev/vda (system partition)...");

    if (wait_for_device("/dev/vda", VDA_WAIT_TIMEOUT) != 0) {
        snprintf(msg, sizeof(msg),
                 "TIMEOUT: /dev/vda did not appear within %d seconds",
                 VDA_WAIT_TIMEOUT);
        klog(msg);
        klog("Listing /dev/vd* devices:");
        /* Try to list what's available */
        system("ls -la /dev/vd* 2>&1 || echo '  none found'");
        /* Don't exit — try to continue so we can see diagnostics */
    } else {
        klog("/dev/vda is available");
    }

    /* ── Step 3: Mount /system (System-as-Root) ─────────────── */

    mkdir("/system", 0755);

    if (mount("/dev/vda", "/system", "ext4", MS_RDONLY, "barrier=1") != 0) {
        snprintf(msg, sizeof(msg),
                 "Failed to mount /dev/vda -> /system: %s",
                 strerror(errno));
        klog(msg);
    } else {
        klog("Mounted /dev/vda -> /system (ext4, ro)");
    }

    /* ── Step 4: Mount /vendor ──────────────────────────────── */

    mkdir("/vendor", 0755);

    if (access("/dev/vdb", F_OK) == 0) {
        if (mount("/dev/vdb", "/vendor", "ext4", MS_RDONLY, "barrier=1") != 0) {
            snprintf(msg, sizeof(msg),
                     "Failed to mount /dev/vdb -> /vendor: %s",
                     strerror(errno));
            klog(msg);
        } else {
            klog("Mounted /dev/vdb -> /vendor (ext4, ro)");
        }
    } else {
        klog("WARNING: /dev/vdb not found, skipping vendor mount");
    }

    /* ── Step 5: Import androidboot.* properties ───────────── */

    import_androidboot_properties();

    /* ── Step 6: Hand off to AOSP /system/bin/init ─────────── */

    klog("Handing off to /system/bin/init selinux_setup");

    const char *init_path = "/system/bin/init";
    char *init_argv[] = {
        (char *)init_path,
        "selinux_setup",
        NULL
    };
    char *init_envp[] = { NULL };

    execve(init_path, init_argv, init_envp);

    /* If execve returns, it failed */
    snprintf(msg, sizeof(msg),
             "FATAL: execve(\"%s\", \"selinux_setup\") failed: %s",
             init_path, strerror(errno));
    klog(msg);

    /* Attempt fallback: exec /system/bin/init without arguments */
    klog("Attempting fallback: /system/bin/init with no args...");
    char *fallback_argv[] = { (char *)init_path, NULL };
    execve(init_path, fallback_argv, init_envp);

    klog("FATAL: All init exec attempts failed. System halted.");
    return 1;
}
