/*
 * SiliconV — syscall() + abort() interception shim
 *
 * LD_PRELOAD library that intercepts:
 *  - syscall(__NR_reboot, ...) — blocks the bootloader reboot
 *    by returning 0 (success) without actually rebooting.
 *  - abort() — prevents init from crashing after failed reboot
 *
 * Without this shim, init's __reboot() function calls abort()
 * if the reboot syscall returns (because reboot isn't supposed
 * to return).  By intercepting abort() to simply return, we
 * let init continue its normal event loop — the reboot attempt
 * becomes a silent no-op.
 *
 * Build (ARM64):
 *   aarch64-linux-gnu-gcc -shared -fPIC -o libsi_intercept.so \
 *       syscall_intercept.c -ldl
 *
 * Usage:
 *   LD_PRELOAD=/vendor/lib64/libsi_intercept.so \
 *       /system/bin/init selinux_setup
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

/* ── syscall() interceptor ───────────────────────────── */

typedef long (*real_syscall_t)(long number, ...);
static real_syscall_t real_syscall = NULL;

long syscall(long number, ...)
{
    va_list args;
    long a[6];
    int i;

    /* One-time lazy resolve of the real syscall in libc */
    if (!real_syscall) {
        real_syscall = (real_syscall_t)dlsym(RTLD_NEXT, "syscall");
        if (!real_syscall) {
            fprintf(stderr, "si_intercept: FATAL: cannot resolve syscall\n");
            _exit(127);
        }
    }

    /* ── Block reboot syscall ── */
    if (number == __NR_reboot) {
        /* Log to stderr so it appears in the boot log */
        fprintf(stderr, "si_intercept: blocked __NR_reboot (reboot)\n");
        /* Return success — init will try to abort(), which we also intercept */
        return 0;
    }

    /* Pass through all other syscalls */
    va_start(args, number);
    for (i = 0; i < 6; i++)
        a[i] = va_arg(args, long);
    va_end(args);

    return real_syscall(number, a[0], a[1], a[2], a[3], a[4], a[5]);
}


/* ── abort() interceptor ─────────────────────────────── */

typedef void (*real_abort_t)(void);
static real_abort_t real_abort = NULL;

void abort(void)
{
    if (!real_abort) {
        real_abort = (real_abort_t)dlsym(RTLD_NEXT, "abort");
        if (!real_abort) {
            fprintf(stderr, "si_intercept: FATAL: cannot resolve abort\n");
            _exit(127);
        }
    }

    fprintf(stderr, "si_intercept: intercepted abort() — continuing\n");

    /*
     * Instead of actually calling abort(), we just return.
     * The caller (init's __reboot function) will fall through
     * to its common exit path and eventually return to the
     * init event loop, allowing boot to continue.
     */
}
