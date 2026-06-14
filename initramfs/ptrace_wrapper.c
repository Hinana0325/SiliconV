/*
 * ptrace_wrapper.c — Traces init via ptrace, logging reboot syscall.
 * Cross-compile: aarch64-linux-gnu-gcc -static -O2 -o ptrace_wrapper ptrace_wrapper.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <signal.h>
#include <errno.h>
#include <elf.h>

#define __NR_reboot 142

static const char *reboot_cmd_name(long cmd)
{
    switch(cmd) {
        case 0x01234567: return "RB_BOOT_CADER (bootloader)";
        case 0x4321FEDC: return "RB_KEXEC (kexec)";
        case 0xDEAD0002: return "RB_HALT_SYSTEM (halt)";
        case 0xDEAD0003: return "RB_POWER_OFF (poweroff)";
        case 0xDEAD0004: return "RB_RESTART (restart)";
        case 0xCDEF0123: return "RB_SW_SUSPEND (sw_suspend)";
        case 0xD000FCE2: return "RB_SUSPEND (suspend)";
        case 0xCDEF0124: return "RB_CAD_ON";
        case 0xCDEF0125: return "RB_CAD_OFF";
        default: {
            static char buf[64];
            snprintf(buf, sizeof(buf), "unknown(0x%lx)", cmd);
            return buf;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        raise(SIGSTOP);
        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "wrapper: child did not stop\n");
        return 1;
    }

    ptrace(PTRACE_SYSCALL, pid, NULL, NULL);

    while(1) {
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            fprintf(stderr, "wrapper: child exited with status %d\n", WEXITSTATUS(status));
            break;
        }
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "wrapper: child killed by signal %d\n", WTERMSIG(status));
            break;
        }
        if (!WIFSTOPPED(status)) {
            fprintf(stderr, "wrapper: unexpected status %d\n", status);
            break;
        }

        int sig = WSTOPSIG(status);
        if (sig != (SIGTRAP | 0x80)) {
            ptrace(PTRACE_SYSCALL, pid, NULL, (void*)(long)sig);
            continue;
        }

        struct user_pt_regs regs;
        struct iovec iov = { .iov_base = &regs, .iov_len = sizeof(regs) };
        ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov);

        if (regs.regs[8] == __NR_reboot) {
            fprintf(stderr, "\n!!! REBOOT SYSCALL: reboot(%ld, %ld, %ld) !!!\n",
                    regs.regs[0], regs.regs[1], regs.regs[2]);
            fprintf(stderr, "    type: %s\n", reboot_cmd_name(regs.regs[0]));
            fprintf(stderr, "    cmd: 0x%lx\n", regs.regs[1]);
            fprintf(stderr, "    arg: 0x%lx\n", regs.regs[2]);
            fprintf(stderr, "    KILLING init to prevent reboot!\n");
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }

        ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
    }

    return 0;
}
