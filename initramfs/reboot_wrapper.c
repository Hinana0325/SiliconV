/*
 * reboot_wrapper.c — Intercepts reboot() syscall, logs args, then executes init.
 * Compile as static ARM64 binary.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <errno.h>

/* Intercept reboot syscall via LD_PRELOAD */
int reboot(int type)
{
    const char *reason = "unknown";
    switch(type) {
        case LINUX_REBOOT_CMD_POWER_OFF: reason = "poweroff"; break;
        case LINUX_REBOOT_CMD_RESTART:   reason = "restart"; break;
        case LINUX_REBOOT_CMD_CAD_ON:    reason = "cad_on"; break;
        case LINUX_REBOOT_CMD_CAD_OFF:   reason = "cad_off"; break;
        case LINUX_REBOOT_CMD_HALT:      reason = "halt"; break;
        case LINUX_REBOOT_CMD_KEXEC:     reason = "kexec"; break;
        case LINUX_REBOOT_CMD_SW_SUSPEND: reason = "suspend"; break;
        case LINUX_REBOOT_CMD_SUSPEND:   reason = "suspend2"; break;
        case 0x01234567:                 reason = "bootloader(RB_BOOT_CADER)"; break;
        case 0x4321FEDC:                 reason = "recovery"; break;
        case 0xDEAD2DEF:                 reason = "coldRestart"; break;
    }
    fprintf(stderr, "\n!!! INTERCEPTED reboot(type=%d, reason=%s) !!!\n", type, reason);
    fprintf(stderr, "!!! Blocking reboot to allow further inspection !!!\n");
    /* Don't actually reboot, just pause */
    while(1) { sleep(1); }
    return 0;
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "reboot_wrapper: PID=%d, about to exec init\n", getpid());
    /* We can't use LD_PRELOAD with static binaries, so this approach 
     * won't work directly. Just exec init. */
    execv("/system/bin/init", argv);
    perror("execv");
    return 1;
}
