/*
 * SiliconV — Android First-Stage Init (Phase 4)
 *
 * Boots AOSP GSI via chroot + selinux_setup handoff.
 * Strategy:
 *   1. Mount proc/sys/dev in initramfs
 *   2. Mount system.img
 *   3. Set up writable tmpfs overlays on existing GSI directories
 *   4. Create minimal vendor SELinux policy files
 *   5. Bind-mount dev/proc/sys into chroot
 *   6. chroot into system partition
 *   7. Exec /system/bin/init selinux_setup
 *
 * Build (ARM64 static):
 *   aarch64-linux-gnu-gcc -static -o init init.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>

static void die(const char *msg)
{
    fprintf(stderr, "initramfs: FATAL: %s: %s\n", msg, strerror(errno));
    while (1) pause();
}

static void write_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "initramfs: cannot create %s: %s\n", path, strerror(errno));
        return;
    }
    write(fd, content, strlen(content));
    close(fd);
}

/*
 * Minimal ld.config.txt for Android 17+ (AOSP GSI).
 * Without this, dynamically-linked binaries abort with linker errors.
 */
static const char *ld_config =
    "# Minimal linker namespace config for Android GSI boot\n"
    "[system]\n"
    "namespace.default.isolated = true\n"
    "namespace.default.search.paths = /system/${LIB}:/system/${LIB}/hw:/system/${LIB}/egl\n"
    "namespace.default.permitted.paths = /system/${LIB}:/system/${LIB}/hw:/system/${LIB}/egl\n"
    "namespace.default.asan.search.paths = /data/asan/system/${LIB}:/system/${LIB}\n"
    "namespace.default.asan.permitted.paths = /data/asan/system/${LIB}:/system/${LIB}\n"
    "\n"
    "[system:apex]\n"
    "namespace.default.apex.search.paths = /apex/com.android.runtime/${LIB}/bionic\n"
    "namespace.default.apex.permitted.paths = /apex/com.android.runtime/${LIB}/bionic\n";

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    fprintf(stderr, "\n=== SiliconV Android First-Stage Init ===\n\n");

    /* ── Mount essential filesystems ───────────────── */
    fprintf(stderr, "initramfs: mounting /proc ...\n");
    if (mount("proc", "/proc", "proc", 0, NULL) < 0)
        die("mount /proc");

    fprintf(stderr, "initramfs: mounting /sys ...\n");
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) < 0)
        die("mount /sys");

    /* Mount selinuxfs so AOSP init can load the SELinux policy.
     * selinuxfs is a separate fs (not part of sysfs) and must be
     * mounted before the recursive bind into the chroot, so the
     * mount propagates into the chroot via MS_REC. */
    fprintf(stderr, "initramfs: mounting selinuxfs ...\n");
    mkdir("/sys/fs/selinux", 0755);
    if (mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", 0, NULL) < 0)
        fprintf(stderr, "initramfs: WARNING selinuxfs mount failed: %s\n", strerror(errno));

    fprintf(stderr, "initramfs: mounting /dev ...\n");
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0)
        die("mount /dev");

    /* ── Mount system partition ────────────────────── */
    fprintf(stderr, "initramfs: mounting system.img (/dev/vda) ...\n");
    if (mkdir("/sysroot", 0755) < 0 && errno != EEXIST)
        die("mkdir /sysroot");

    /* Mount system read-only. The GSI ext4 has INCOMPAT_LARGEDIR (0x4000)
     * which prevents read-write mounting with this kernel. We then overlay
     * writable tmpfs on directories that AOSP init needs to modify. */
    if (mount("/dev/vda", "/sysroot", "ext4", MS_RDONLY, NULL) < 0)
        die("mount /sysroot (/dev/vda)");

    /* ── Prepare writable overlays on system root ──── */
    fprintf(stderr, "initramfs: preparing writable overlays ...\n");

    /*
     * Bind-mount dev/proc/sys so AOSP init (selinux_setup) can:
     * - Read /proc/device-tree for fstab
     * - Read /proc/mounts
     * - Write to /dev/kmsg and create /dev/__properties__
     * - Mount selinuxfs under /sys/fs/selinux
     * These directories already exist on the GSI, so bind targets are valid.
     */
    mount("/dev", "/sysroot/dev", NULL, MS_BIND | MS_REC, NULL);
    mount("/proc", "/sysroot/proc", NULL, MS_BIND | MS_REC, NULL);
    mount("/sys", "/sysroot/sys", NULL, MS_BIND | MS_REC, NULL);

    /* /vendor - mount tmpfs for writable vendor overlay */
    mount("tmpfs", "/sysroot/vendor", "tmpfs", 0, NULL);
    /* Create SELinux policy directory structure */
    mkdir("/sysroot/vendor/etc", 0755);
    mkdir("/sysroot/vendor/etc/selinux", 0755);

    /*
     * Bind-mount patched plat_sepolicy.cil over the GSI's original
     * (which has duplicate typeattribute + unsupported policycap).
     * Using bind-mount instead of tmpfs overlay so the mapping/
     * subdirectory and other files remain visible.
     */
    if (mount("/selinux_policy/plat_sepolicy_patched.cil",
              "/sysroot/system/etc/selinux/plat_sepolicy.cil",
              NULL, MS_BIND, NULL) == 0) {
        fprintf(stderr, "initramfs: patched plat_sepolicy.cil bind-mounted\n");
    } else {
        fprintf(stderr, "initramfs: WARNING could not bind-mount patched CIL\n");
    }

    /*
     * Also need plat_sepolicy_vers.txt for CIL compilation.
     * Version 34.0 maps to the mapping/34.0.cil file on the GSI.
     */
    write_file("/sysroot/vendor/etc/selinux/plat_sepolicy_vers.txt",
               "34.0\n");

    /*
     * Copy precompiled SELinux policy (compiled on host from patched CIL)
     * into the vendor overlay so init can load it directly.
     */
    {
        const char *src = "/selinux_policy/precompiled_sepolicy";
        const char *dst = "/sysroot/vendor/etc/selinux/precompiled_sepolicy";
        int sfd = open(src, O_RDONLY);
        if (sfd >= 0) {
            int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (dfd >= 0) {
                char buf[4096];
                ssize_t n;
                while ((n = read(sfd, buf, sizeof(buf))) > 0) {
                    write(dfd, buf, (size_t)n);
                }
                close(dfd);
                fprintf(stderr, "initramfs: precompiled_sepolicy installed\n");
            }
            close(sfd);
        } else {
            fprintf(stderr, "initramfs: WARNING precompiled_sepolicy not found\n");
        }
    }

    /*
     * Create SHA256 companion files for precompiled sepolicy verification.
     * Hash = SHA256(patched plat_sepolicy.cil || mapping/34.0.cil)
     */
    write_file("/sysroot/vendor/etc/selinux/"
               "precompiled_sepolicy.plat_sepolicy_and_mapping.sha256",
               "03f08c8d1aff00a2b88730df4cb00d63f44b3e8194d9dc84aa3991d27dd2edeb\n");

    /*
     * Create product + system_ext sha256 companion files matching the
     * GSI's original hashes so precompiled sepolicy verification passes.
     */
    write_file("/sysroot/vendor/etc/selinux/"
               "precompiled_sepolicy.product_sepolicy_and_mapping.sha256",
               "75a11da44c802486bc6f65640aa48a730f0f684c5c07a42ba3cd1735eb3fb070\n");
    write_file("/sysroot/vendor/etc/selinux/"
               "precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256",
               "75a11da44c802486bc6f65640aa48a730f0f684c5c07a42ba3cd1735eb3fb070\n");

    /*
     * Bind-mount a patched sha256 file over the system's original,
     * so both sha256 files match our patched CIL content.
     */
    if (mount("/selinux_policy/plat_sepolicy_and_mapping.sha256",
              "/sysroot/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
              NULL, MS_BIND, NULL) == 0) {
        fprintf(stderr, "initramfs: sha256 file bind-mounted\n");
    }

    /*
     * Also provide plat_sepolicy_vers.txt and vendor_sepolicy.cil as
     * fallback in case the precompiled path fails.
     */
    write_file("/sysroot/vendor/etc/selinux/plat_sepolicy_vers.txt",
               "34.0\n");
    write_file("/sysroot/vendor/etc/selinux/vendor_sepolicy.cil",
               ";; Empty vendor SELinux policy for SiliconV GSI boot\n");

    /* /linkerconfig - tmpfs for linker namespace config */
    mount("tmpfs", "/sysroot/linkerconfig", "tmpfs", 0, NULL);
    write_file("/sysroot/linkerconfig/ld.config.txt", ld_config);

    /* /mnt - tmpfs so SetupMountNamespaces can create /mnt/user,
     * /mnt/installer, /mnt/androidwritable (mkdir_recursive fails on
     * read-only ext4). */
    mount("tmpfs", "/sysroot/mnt", "tmpfs", 0, NULL);

    /* /tmp - tmpfs for temporary files AOSP init may create */
    mkdir("/sysroot/tmp", 0755);
    mount("tmpfs", "/sysroot/tmp", "tmpfs", 0, NULL);

    /*
     * Provide fstab.siliconv at /vendor/etc/fstab.siliconv.
     * The init tries /system/etc/fstab.<hw> first, then
     * /vendor/etc/fstab.<hw>.  Since /system is read-only ext4,
     * we use the vendor tmpfs path instead.
     */
    /*
     * Provide fstab.siliconv at /vendor/etc/fstab.siliconv.
     * Must contain at least one valid entry for the parser.
     */
    write_file("/sysroot/vendor/etc/fstab.siliconv",
               "# fstab for SiliconV VM\n"
               "/dev/vda /system ext4 ro,barrier=1 wait,first_stage_mount\n");

    /*
     * Provide init.siliconv.rc at /vendor/etc/init/hw/init.siliconv.rc.
     * The GSI init.rc imports /vendor/etc/init/hw/init.${ro.hardware}.rc
     * for device-specific early-init and init commands.
     */
    mkdir("/sysroot/vendor/etc/init", 0755);
    mkdir("/sysroot/vendor/etc/init/hw", 0755);
    write_file("/sysroot/vendor/etc/init/hw/init.siliconv.rc",
               "# SiliconV VM init script\n"
               "on early-init\n"
               "    # VM device nodes already configured\n"
               "\n"
               "on init\n"
               "    # No additional partitions to mount\n"
               "    # All partitions are pre-mounted by initramfs\n"
               "\n"
               "on late-init\n"
               "    # Start core services\n"
               "    start ueventd\n");

    /* /apex - tmpfs for APEX runtime overlays */
    mount("tmpfs", "/sysroot/apex", "tmpfs", 0, NULL);
    mkdir("/sysroot/apex/com.android.runtime", 0755);
    mkdir("/sysroot/apex/com.android.runtime/bin", 0755);
    symlink("/system/bin/bootstrap/linker64",
            "/sysroot/apex/com.android.runtime/bin/linker64");
    mkdir("/sysroot/apex/com.android.runtime/lib64", 0755);
    symlink("/system/lib64/bootstrap/libc.so",
            "/sysroot/apex/com.android.runtime/lib64/bionic/libc.so");

    /* /data - tmpfs for writable data */
    mount("tmpfs", "/sysroot/data", "tmpfs", 0, NULL);

    /* /metadata - tmpfs for checkpoint/A/B metadata.
     * Init needs this for checkpoint system; without it, it tries
     * to reboot to bootloader. */
    mkdir("/sysroot/metadata", 0700);
    mount("tmpfs", "/sysroot/metadata", "tmpfs", 0, NULL);
    mkdir("/sysroot/metadata/ota", 0700);

    /* /cache - tmpfs for cache (some services expect this) */
    mkdir("/sysroot/cache", 0770);
    mount("tmpfs", "/sysroot/cache", "tmpfs", 0, NULL);

    /* /persist - tmpfs for persistent data */
    mkdir("/sysroot/persist", 0771);
    mount("tmpfs", "/sysroot/persist", "tmpfs", 0, NULL);

    /*
     * Write build.prop overrides to prevent A/B slot reboot behavior.
     * The GSI expects A/B slot management and will try to reboot to
     * bootloader if it can't find /misc or /metadata for checkpoint.
     * Set ro.boot.slot_suffix and related props to prevent this.
     */

    /*
     * Create /first_stage_ramdisk so second stage init knows first
     * stage completed successfully. Without this, init may decide
     * it was not properly initialized and reboot to bootloader. */
    mkdir("/sysroot/first_stage_ramdisk", 0755);

    /*
     * Create /misc as tmpfs so bootloader_message writes don't fail.
     * Init tries to write 'bootonce-bootloader' to /misc when it
     * wants to reboot; if /misc doesn't exist, it still proceeds
     * with the reboot anyway. But having /misc may prevent some
     * early-init code paths from triggering the reboot. */
    mkdir("/sysroot/misc", 0755);
    mount("tmpfs", "/sysroot/misc", "tmpfs", 0, NULL);

    /*
     * /dev/__properties__ for Android property service.
     * Must be a tmpfs mount, not a regular dir on devtmpfs, because
     * the SELinux policy only allows associate (creating files with
     * different labels) on type tmpfs, not on type device (devtmpfs).
     * A separate tmpfs mount gives it type tmpfs, which the policy
     * permits for property file contexts.
     */
    mkdir("/sysroot/dev/__properties__", 0755);
    mount("tmpfs", "/sysroot/dev/__properties__", "tmpfs", 0, NULL);

    /* /dev/socket for Unix-domain sockets (property service, ueventd, etc.) */
    mkdir("/sysroot/dev/socket", 0755);

    /* Create /dev/kmsg for AOSP init logging during selinux_setup */
    {
        int fd = open("/sysroot/dev/kmsg", O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) close(fd);
    }

    /*
     * Pre-label /dev nodes with correct SELinux contexts so AOSP init
     * (user build, always enforcing) can access them before ueventd runs.
     * This is done BEFORE policy load, while SELinux is still in its
     * initial permissive state, so setxattr should succeed.
     */
    {
        const char *ctx_kmsg = "u:object_r:kmsg_device:s0";
        const char *ctx_null = "u:object_r:null_device:s0";
        int ret;

        ret = setxattr("/sysroot/dev/kmsg", "security.selinux",
                       ctx_kmsg, strlen(ctx_kmsg), 0);
        fprintf(stderr, "initramfs: set context on /dev/kmsg: %s\n",
                ret == 0 ? "OK" : strerror(errno));

        ret = setxattr("/sysroot/dev/null", "security.selinux",
                       ctx_null, strlen(ctx_null), 0);
        fprintf(stderr, "initramfs: set context on /dev/null: %s\n",
                ret == 0 ? "OK" : strerror(errno));
    }

    /* ── Display cmdline ───────────────────────────── */
    {
        FILE *f = fopen("/proc/cmdline", "r");
        if (f) {
            char buf[1024];
            if (fgets(buf, sizeof(buf), f)) {
                buf[strcspn(buf, "\n")] = '\0';
                fprintf(stderr, "initramfs: cmdline: %s\n", buf);
            }
            fclose(f);
        }
    }

    /* ── Chroot into Android system ────────────────── */
    fprintf(stderr, "\ninitramfs: chrooting into /sysroot ...\n\n");

    if (chdir("/sysroot") < 0)
        die("chdir /sysroot");
    if (chroot(".") < 0)
        die("chroot");
    if (chdir("/") < 0)
        die("chdir /");

    /*
     * Now running inside the GSI root.
     * /dev, /proc, /sys are bind-mounted from initramfs.
     * /vendor, /linkerconfig, /apex, /data are tmpfs.
     */

    /* ── Execute AOSP init at selinux_setup phase ──── */
    fprintf(stderr, "initramfs: executing /system/bin/init selinux_setup ...\n\n");

    {
        char *const argv[] = { "/system/bin/init", "selinux_setup", NULL };
        execv("/system/bin/init", argv);
    }

    /* Fallback: try /init symlink */
    {
        char *const argv[] = { "/init", "selinux_setup", NULL };
        execv("/init", argv);
    }

    die("exec init");
    return 1;
}
