/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SiliconV Android Boot Initramfs - First Stage Init
 *
 * This program serves as the initramfs /init for booting AOSP GSI in QEMU.
 * It mounts the necessary partitions, sets up the filesystem tree,
 * then hands off to /system/bin/init (AOSP's init) via chroot+exec.
 *
 * Build:
 *   aarch64-linux-gnu-gcc -static -Os -o init android_initramfs_init.c
 *
 * Expected block device mapping (QEMU virtio):
 *   /dev/vda  -> system.img   (mounted at /sysroot)
 *   /dev/vdb  -> vendor.img   (mounted at /sysroot/vendor)
 *   /dev/vdc  -> userdata.img (mounted at /sysroot/data)
 *   /dev/vdd  -> cache.img    (mounted at /sysroot/cache)
 *   /dev/vde  -> metadata.img (mounted at /sysroot/metadata)
 *
 * Kernel cmdline parameters used:
 *   androidboot.hardware=    - hardware name for fstab lookup
 *   androidboot.selinux=     - SELinux mode (permissive/disabled)
 *   androidboot.veritymode=  - dm-verity mode
 *   androidboot.force_normal_boot=1 - skip recovery
 *   sv_init.debug=1          - enable verbose debugging
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/sysmacros.h>

/* Default block device mapping */
#define DEV_SYSTEM   "/dev/vda"
#define DEV_VENDOR   "/dev/vdb"
#define DEV_DATA     "/dev/vdc"
#define DEV_CACHE    "/dev/vdd"
#define DEV_METADATA "/dev/vde"

/* Mount points under sysroot */
#define ROOT_MNT     "/sysroot"
#define VENDOR_MNT   "/sysroot/vendor"
#define DATA_MNT     "/sysroot/data"
#define CACHE_MNT    "/sysroot/cache"
#define META_MNT     "/sysroot/metadata"

/* Partition image sizes (from build_android_images.sh) */
#define SYSTEM_SIZE  "1890M"
#define VENDOR_SIZE  "64M"
#define USERDATA_SIZE "1G"
#define CACHE_SIZE   "128M"
#define METADATA_SIZE "16M"

static int debug = 0;

#define DBG(fmt, ...) do { if (debug) \
    printf("[sv_init] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG(fmt, ...) printf("[sv_init] " fmt "\n", ##__VA_ARGS__)

#define DIE(fmt, ...) do { \
    LOG("FATAL: " fmt, ##__VA_ARGS__); \
    sleep(2); sync(); reboot(LINUX_REBOOT_CMD_POWER_OFF); \
    exit(1); } while (0)

/* Read kernel cmdline parameter */
static const char* get_cmdline_val(const char* key)
{
    static char buf[4096];
    static char val[256];
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) return NULL;

    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return NULL;
    buf[n] = '\0';

    char *p = buf;
    while (*p) {
        /* Skip spaces */
        while (*p == ' ') p++;
        if (!*p) break;

        /* Check if this token starts with the key */
        if (strncmp(p, key, strlen(key)) == 0 && p[strlen(key)] == '=') {
            p += strlen(key) + 1;
            char *v = val;
            while (*p && *p != ' ') {
                *v++ = *p++;
            }
            *v = '\0';
            return val;
        }

        /* Skip to next space */
        while (*p && *p != ' ') p++;
    }
    return NULL;
}

/* Create directory and all parents */
static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    /* Strip trailing slash */
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}

/* Mount a filesystem with retry */
static int mount_retry(const char *source, const char *target,
                       const char *type, unsigned long flags,
                       const void *data, int retries)
{
    int rc;
    for (int i = 0; i < retries; i++) {
        rc = mount(source, target, type, flags, data);
        if (rc == 0) return 0;
        if (i < retries - 1) {
            DBG("mount %s -> %s failed (attempt %d), retrying...",
                source, target, i + 1);
            usleep(100000); /* 100ms */
        }
    }
    return rc;
}

/* Mount a block device with ext4 */
static int mount_ext4(const char *dev, const char *mntpt, int readonly)
{
    unsigned long flags = readonly ? MS_RDONLY : 0;
    const char *data = readonly ? NULL : "rw";

    if (mkdir_p(mntpt, 0755) != 0 && errno != EEXIST) {
        LOG("Failed to create %s: %s", mntpt, strerror(errno));
        return -1;
    }

    int rc = mount_retry(dev, mntpt, "ext4", flags, data, 3);
    if (rc != 0) {
        LOG("Failed to mount %s at %s: %s", dev, mntpt, strerror(errno));
        return -1;
    }
    DBG("Mounted %s at %s (%s)", dev, mntpt, readonly ? "ro" : "rw");
    return 0;
}

/* Mount a tmpfs */
static int mount_tmpfs(const char *mntpt, size_t size_kb)
{
    char data[64];
    snprintf(data, sizeof(data), "size=%zuk", size_kb);

    if (mkdir_p(mntpt, 0755) != 0 && errno != EEXIST) {
        LOG("Failed to create %s: %s", mntpt, strerror(errno));
        return -1;
    }

    if (mount("tmpfs", mntpt, "tmpfs", 0, data) != 0) {
        LOG("Failed to mount tmpfs at %s: %s", mntpt, strerror(errno));
        return -1;
    }
    DBG("Mounted tmpfs at %s (%zuk)", mntpt, size_kb);
    return 0;
}

/* Check if a block device exists */
static int dev_exists(const char *dev)
{
    struct stat st;
    return (stat(dev, &st) == 0 && S_ISBLK(st.st_mode));
}

/* Wait for a block device to appear */
static int wait_for_dev(const char *dev, int timeout_sec)
{
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (dev_exists(dev)) return 1;
        usleep(100000); /* 100ms */
    }
    return 0;
}

/* Ensure /dev/console is available for AOSP init */
static int setup_console(void)
{
    struct stat st;
    if (stat("/dev/console", &st) != 0) {
        /* Try to create it */
        mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
    }
    /* Also ensure ttyAMA0 exists */
    if (stat("/dev/ttyAMA0", &st) != 0) {
        mknod("/dev/ttyAMA0", S_IFCHR | 0600, makedev(204, 64));
    }
    return 0;
}

/* Create device nodes needed by AOSP init */
static int setup_devices(void)
{
    /* /dev/loop-control for snapuserd */
    struct stat st;
    if (stat("/dev/loop-control", &st) != 0) {
        mknod("/dev/loop-control", S_IFCHR | 0600, makedev(10, 237));
    }
    /* /dev/loop0..7 for snapuserd */
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/loop%d", i);
        if (stat(path, &st) != 0) {
            mknod(path, S_IFBLK | 0600, makedev(7, i));
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    LOG("SiliconV Android Boot Initramfs v1.0");

    /* Check for debug flag in cmdline */
    const char *dbg_val = get_cmdline_val("sv_init.debug");
    if (dbg_val && strcmp(dbg_val, "1") == 0) {
        debug = 1;
    }

    /* Mount essential virtual filesystems */
    LOG("Mounting virtual filesystems...");
    mount("none", "/proc", "proc", 0, NULL);
    mount("none", "/sys", "sysfs", 0, NULL);
    mount("none", "/dev", "devtmpfs", 0, NULL);

    /* Ensure console and device nodes */
    setup_console();
    setup_devices();

    /* Print kernel info */
    {
        char buf[256];
        int fd = open("/proc/version", O_RDONLY);
        if (fd >= 0) {
            int n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                /* Remove trailing newline */
                buf[strcspn(buf, "\n")] = '\0';
                LOG("Kernel: %s", buf);
            }
        }
    }

    /* Check for androidboot.hardware in cmdline */
    const char *hardware = get_cmdline_val("androidboot.hardware");
    LOG("androidboot.hardware = %s", hardware ? hardware : "(not set)");

    /* List available block devices */
    LOG("Available block devices:");
    DIR *dir = opendir("/dev");
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == 'v' && de->d_name[1] == 'd') {
                char path[32];
                snprintf(path, sizeof(path), "/dev/%s", de->d_name);
                struct stat st;
                if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
                    LOG("  %s (size: %llu MB)",
                        path, (unsigned long long)st.st_size / (1024 * 1024));
                }
            }
        }
        closedir(dir);
    }

    /* Read kernel cmdline */
    {
        char buf[1024];
        int fd = open("/proc/cmdline", O_RDONLY);
        if (fd >= 0) {
            int n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                DBG("Cmdline: %s", buf);
            }
        }
    }

    /* Wait for block devices */
    LOG("Waiting for block devices...");
    if (!wait_for_dev(DEV_SYSTEM, 5)) {
        DIE("System device %s not found", DEV_SYSTEM);
    }
    if (!wait_for_dev(DEV_VENDOR, 3)) {
        LOG("WARNING: %s not found, continuing without vendor", DEV_VENDOR);
    }

    /* Create mount point root */
    LOG("Mounting partitions...");
    mkdir_p(ROOT_MNT, 0755);

    /* Mount system (read-write for first-stage init) */
    if (mount_ext4(DEV_SYSTEM, ROOT_MNT, 0) != 0) {
        DIE("Cannot mount system partition");
    }

    /* Mount vendor (read-only) */
    if (dev_exists(DEV_VENDOR)) {
        mount_ext4(DEV_VENDOR, VENDOR_MNT, 1);
    }

    /* Mount data, cache, metadata (formattable) */
    if (dev_exists(DEV_DATA)) mount_ext4(DEV_DATA, DATA_MNT, 0);
    if (dev_exists(DEV_CACHE)) mount_ext4(DEV_CACHE, CACHE_MNT, 0);
    if (dev_exists(DEV_METADATA)) mount_ext4(DEV_METADATA, META_MNT, 0);

    /* Bind-mount /dev into new root so AOSP init can write to console.
     * Do NOT pre-mount /proc or /sys - AOSP init will mount them. */
    mkdir_p(ROOT_MNT "/dev", 0755);
    mount("/dev", ROOT_MNT "/dev", NULL, MS_BIND | MS_REC, NULL);

    /* Create /linkerconfig directory with ld.config.txt for AOSP's dynamic linker.
     * In normal Android boot, first-stage init runs /system/bin/linkerconfig to
     * generate this file. Since we bypass first-stage init and go directly to
     * selinux_setup, we must provide it ourselves. Without it, secilc (the SELinux
     * CIL compiler) cannot find its shared libraries (libsepol, etc.) and crashes,
     * causing init to reboot to bootloader. */
    {
        mkdir_p(ROOT_MNT "/linkerconfig", 0755);
        int lcfd = open(ROOT_MNT "/linkerconfig/ld.config.txt",
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (lcfd >= 0) {
            /* Write a minimal config - Android linker will use default namespaces
             * when this file exists but contains no matching section. */
            const char *lc_content =
                "# Android linker configuration\n"
                "# Generated by SiliconV initramfs\n"
                "[default]\n"
                "additional_shared_libraries = \n"
                "\n";
            write(lcfd, lc_content, strlen(lc_content));
            close(lcfd);
            LOG("Created /linkerconfig/ld.config.txt for linker");
        } else {
            LOG("WARNING: Could not create linkerconfig: %s", strerror(errno));
        }
    }

    /* Pre-create /file_contexts for secilc. The SELinux CIL compiler (secilc)
     * writes the compiled file_contexts binary to this path. If it doesn't exist
     * or the directory isn't writable, secilc fails with "Failed to open 
     * file_contexts file". We pre-create it as an empty file so secilc can 
     * truncate and overwrite it. */
    {
        int fcfd = open(ROOT_MNT "/file_contexts",
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fcfd >= 0) {
            close(fcfd);
            LOG("Created /file_contexts for secilc");
        } else {
            LOG("WARNING: Could not create /file_contexts: %s (errno=%d)",
                strerror(errno), errno);
        }
    }

    /* Verify the AOSP init exists */
    struct stat st;
    char init_path[256];

    /* Try various possible init locations */
    const char *init_candidates[] = {
        ROOT_MNT "/system/bin/init",
        ROOT_MNT "/init",
        NULL
    };

    const char *init_bin = NULL;
    for (int i = 0; init_candidates[i]; i++) {
        if (stat(init_candidates[i], &st) == 0 && S_ISREG(st.st_mode)) {
            init_bin = init_candidates[i];
            break;
        }
        /* Also check symlinks */
        if (stat(init_candidates[i], &st) == 0) {
            init_bin = init_candidates[i];
            break;
        }
    }

    if (!init_bin) {
        LOG("Contents of system root:");
        dir = opendir(ROOT_MNT);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                if (de->d_name[0] == '.') continue;
                LOG("  %s/%s", ROOT_MNT, de->d_name);
            }
            closedir(dir);
        }
        DIE("AOSP init not found in system partition");
    }

    LOG("Found AOSP init: %s", init_bin);

    /* Extract the actual init binary path relative to new root */
    /* init_bin is like /sysroot/system/bin/init, we need /system/bin/init */
    const char *new_init = init_bin + strlen(ROOT_MNT);
    if (*new_init == '\0') new_init = "/init";

    LOG("Chrooting to %s and executing %s", ROOT_MNT, new_init);
    DBG("Chrooting to %s...", ROOT_MNT);

    /* Sync and prepare to hand off */
    sync();

    /* Chroot into the new root */
    if (chdir(ROOT_MNT) != 0) {
        DIE("chdir(%s) failed: %s", ROOT_MNT, strerror(errno));
    }
    if (chroot(".") != 0) {
        DIE("chroot(%s) failed: %s", ROOT_MNT, strerror(errno));
    }

    /* Set up /dev, /proc, /sys in chroot */
    /* (should be bind-mounted already) */

    LOG("Executing AOSP init (selinux_setup phase): %s", new_init);

    /* Exec AOSP init at selinux_setup phase, skipping first-stage mount
     * and pivot_root since we already mounted all partitions.
     * This bypasses the fstab/pivot_root/symlink issue entirely. */
    char *const init_argv[] = { (char*)new_init, (char*)"selinux_setup", NULL };
    execv(new_init, init_argv);

    /* If exec fails, try second_stage directly */
    LOG("selinux_setup failed (%s), trying second_stage...", strerror(errno));
    char *const init_argv2[] = { (char*)new_init, (char*)"second_stage", NULL };
    execv(new_init, init_argv2);

    /* If exec fails */
    LOG("execv(%s) failed: %s", new_init, strerror(errno));
    DIE("Cannot execute AOSP init");
}
