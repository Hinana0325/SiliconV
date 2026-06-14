/*
 * shim_reboot.c — LD_PRELOAD shim to intercept reboot() syscall and log it.
 * Cross-compile: aarch64-linux-gnu-gcc -shared -fPIC -o shim_reboot.so shim_reboot.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

/* Override the reboot() libc function to log and block */
int reboot(int type)
{
    const char *reason;
    char buf[128];
    
    switch(type) {
        case LINUX_REBOOT_CMD_POWER_OFF: reason = "poweroff"; break;
        case LINUX_REBOOT_CMD_RESTART:   reason = "restart"; break;
        case LINUX_REBOOT_CMD_HALT:      reason = "halt"; break;
        default:
            snprintf(buf, sizeof(buf), "unknown(0x%x)", type);
            reason = buf;
            break;
    }
    fprintf(stderr, "\n!!! SHIM: reboot(%d) intercepted, reason=%s !!!\n", type, reason);
    fprintf(stderr, "!!! Blocking reboot - sleeping forever !!!\n");
    fflush(stderr);
    while(1) { sleep(100); }
    return 0;
}
