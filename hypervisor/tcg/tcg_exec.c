/*
 * SiliconV — TCG Execution Loop
 *
 * The heart of the TCG backend: fetches, decodes, and executes ARM64
 * instructions in a loop until a VM exit occurs (MMIO, HLT, shutdown).
 *
 * This is Tier 0: a simple switch-based interpreter.
 * For common hot paths (kernel tight loops), we count executions and
 * will JIT-compile them in Tier 2.
 */

#include "tcg.h"
#include "tcg_cpu.h"
#include "tcg_decode.h"
#include "tcg_mmu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Forward declarations ──────────────────────────────── */
static int tcg_step(tcg_vcpu_t *vcpu);

/* ── Main execution loop ───────────────────────────────── */
int tcg_vcpu_run(sv_vcpu_t *vcpu_, sv_vcpu_exit_t *exit)
{
    tcg_vcpu_t *vcpu = (tcg_vcpu_t *)vcpu_;
    if (!vcpu || !vcpu->vm) return -1;

    tcg_vm_t *vm __attribute__((unused)) = vcpu->vm;
    vcpu->running = true;
    vcpu->insn_count = 0;

    /* Set initial timer */
    vcpu->cntvct_el0 = (uint64_t)(clock() * (vcpu->cntfrq_el0 / (double)CLOCKS_PER_SEC));

    printf("tcg: vCPU %d entering execution loop (PC=0x%lx)\n",
           vcpu->id, (unsigned long)vcpu->pc);

    int result = 0;

    while (vcpu->running) {
        /* Check timer IRQ */
        if (vcpu->cntv_ctl_el0 & 1) {  /* Timer enabled */
            vcpu->cntvct_el0 = (uint64_t)(clock() * (vcpu->cntfrq_el0 / (double)CLOCKS_PER_SEC));
            if (vcpu->cntvct_el0 >= vcpu->cntv_cval_el0) {
                vcpu->timer_irq_pending = true;
                /* In full implementation: inject IRQ 27 (virtual timer) */
            }
        }

        int step_ret = tcg_step(vcpu);

        if (step_ret < 0) {
            /* Fatal error */
            fprintf(stderr, "tcg: vCPU %d fatal error at PC=0x%lx\n",
                    vcpu->id, (unsigned long)vcpu->pc);
            result = -1;
            break;
        }

        if (step_ret > 0) {
            /* Exit occurred (MMIO, HLT, SHUTDOWN) */
            if (exit) {
                *exit = vcpu->exit;
            }
            break;
        }
        /* step_ret == 0: continue */
    }

    printf("tcg: vCPU %d stopped (insns: %lu, exits: %lu, mmio: %lu)\n",
           vcpu->id, vcpu->insn_count, vcpu->exit_count, vcpu->mmio_exits);

    return result;
}

/* ── Single instruction step ───────────────────────────── */
static int tcg_step(tcg_vcpu_t *vcpu)
{
    tcg_vm_t *vm = vcpu->vm;

    /* Fetch instruction from guest memory */
    uint32_t insn;
    if (tcg_mmu_read(vm, vcpu->pc, &insn, 4) < 0) {
        fprintf(stderr, "tcg: instruction fetch failed at PC=0x%lx\n",
                (unsigned long)vcpu->pc);
        return -1;
    }

    vcpu->pc += 4;
    vcpu->insn_count++;

    /* Decode and execute */
    return tcg_decode_exec(vcpu, vm, insn);
}
