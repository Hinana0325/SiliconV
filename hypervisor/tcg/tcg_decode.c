/*
 * SiliconV — TCG ARM64 Instruction Decoder
 *
 * Decodes A64 instructions and dispatches to executors.
 * Covers the minimal set needed for Linux kernel boot:
 *   - Data processing (add/sub/and/orr/eor, movz/movk, adr/adrp)
 *   - Loads/stores (ldr/str, ldp/stp)
 *   - Branches (b/bl/br/blr/ret, cbz/cbnz, tbz/tbnz)
 *   - System (mrs/msr, svc, eret, isb)
 *
 * Reference: Arm Architecture Reference Manual (Armv8-A)
 */

#include "tcg.h"
#include <time.h>

#include "tcg_cpu.h"
#include "tcg_mmu.h"

#include <stdio.h>
#include <string.h>

/* ── A64 Decode Helpers ─────────────────────────────────── */

/* Extract bit field */
static inline uint32_t bits(uint32_t val, int hi, int lo) {
    return (val >> lo) & ((1U << (hi - lo + 1)) - 1);
}
static inline uint32_t bit(uint32_t val, int pos) {
    return (val >> pos) & 1;
}

/* Sign-extend an N-bit value to 64 bits */
static inline int64_t sext(int64_t val, int n) {
    return (val << (64 - n)) >> (64 - n);
}

/* Zero-extend */
static inline uint64_t zext(uint64_t val, int n) {
    return val & ((1ULL << n) - 1);
}

/* ── PSTATE helpers ─────────────────────────────────────── */
static uint32_t get_el(tcg_vcpu_t *vcpu) {
    return vcpu->pstate & PSTATE_EL_MASK;
}

static uint64_t get_sp(tcg_vcpu_t *vcpu) {
    return (get_el(vcpu) >= 1) ? vcpu->sp_el1 : vcpu->sp_el0;
}

static void set_sp(tcg_vcpu_t *vcpu, uint64_t val) {
    if (get_el(vcpu) >= 1) vcpu->sp_el1 = val;
    else vcpu->sp_el0 = val;
}

/* ── Condition flag helpers ─────────────────────────────── */
static void set_nzcv(tcg_vcpu_t *vcpu, int n, int z, int c, int v) {
    vcpu->pstate &= ~(PSTATE_N | PSTATE_Z | PSTATE_C | PSTATE_V);
    if (n) vcpu->pstate |= PSTATE_N;
    if (z) vcpu->pstate |= PSTATE_Z;
    if (c) vcpu->pstate |= PSTATE_C;
    if (v) vcpu->pstate |= PSTATE_V;
}

static void set_nz(tcg_vcpu_t *vcpu, uint64_t val) {
    vcpu->pstate &= ~(PSTATE_N | PSTATE_Z);
    if ((int64_t)val < 0)  vcpu->pstate |= PSTATE_N;
    if (val == 0)          vcpu->pstate |= PSTATE_Z;
}

static void set_add_flags(tcg_vcpu_t *vcpu, uint64_t a, uint64_t b, uint64_t result) {
    int n = ((int64_t)result < 0);
    int z = (result == 0);
    int c = (result < a);  /* Unsigned carry */
    int v = (((a ^ result) & (b ^ result)) >> 63) & 1;  /* Signed overflow */
    set_nzcv(vcpu, n, z, c, v);
}


/* ══════════════════════════════════════════════════════════
 *  System Register Dispatch
 * ══════════════════════════════════════════════════════════ */

#include <time.h>

static int handle_mrs(tcg_vcpu_t *vcpu, uint32_t sysreg, uint8_t rt)
{
    /* System register encoding:
     * [19..0] = Op0(2):Op1(3):CRn(4):CRm(4):Op2(3)
     * Op0=11 for MRS
     */
    uint8_t op0 = (sysreg >> 17) & 0x3;
    uint8_t op1 = (sysreg >> 14) & 0x7;
    uint8_t crn = (sysreg >> 10) & 0xf;
    uint8_t crm = (sysreg >> 6) & 0xf;
    uint8_t op2 = (sysreg >> 3) & 0x7;
    uint8_t rt_field = sysreg & 0x7;  /* This should match rt */

    (void)rt_field;

    /* CurrentEL: op0=3, op1=0, crn=4, crm=2, op2=2 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 2 && op2 == 2) {
        vcpu->x[rt] = get_el(vcpu) << 2;
        return 0;
    }

    /* SCTLR_EL1: op0=3, op1=0, crn=1, crm=0, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 1 && crm == 0 && op2 == 0) {
        vcpu->x[rt] = vcpu->sctlr_el1;
        return 0;
    }

    /* TTBR0_EL1: op0=3, op1=0, crn=2, crm=0, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 2 && crm == 0 && op2 == 0) {
        vcpu->x[rt] = vcpu->ttbr0_el1;
        return 0;
    }

    /* TTBR1_EL1: op0=3, op1=0, crn=2, crm=0, op2=1 */
    if (op0 == 3 && op1 == 0 && crn == 2 && crm == 0 && op2 == 1) {
        vcpu->x[rt] = vcpu->ttbr1_el1;
        return 0;
    }

    /* TCR_EL1: op0=3, op1=0, crn=2, crm=0, op2=2 */
    if (op0 == 3 && op1 == 0 && crn == 2 && crm == 0 && op2 == 2) {
        vcpu->x[rt] = vcpu->tcr_el1;
        return 0;
    }

    /* MAIR_EL1: op0=3, op1=0, crn=10, crm=2, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 10 && crm == 2 && op2 == 0) {
        vcpu->x[rt] = vcpu->mair_el1;
        return 0;
    }

    /* VBAR_EL1: op0=3, op1=0, crn=12, crm=0, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 12 && crm == 0 && op2 == 0) {
        vcpu->x[rt] = vcpu->vbar_el1;
        return 0;
    }

    /* ELR_EL1: op0=3, op1=0, crn=4, crm=0, op2=1 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 0 && op2 == 1) {
        vcpu->x[rt] = vcpu->elr_el1;
        return 0;
    }

    /* SPSR_EL1: op0=3, op1=0, crn=4, crm=0, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 0 && op2 == 0) {
        vcpu->x[rt] = vcpu->spsr_el1;
        return 0;
    }

    /* ESR_EL1: op0=3, op1=0, crn=5, crm=2, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 5 && crm == 2 && op2 == 0) {
        vcpu->x[rt] = vcpu->esr_el1;
        return 0;
    }

    /* FAR_EL1: op0=3, op1=0, crn=6, crm=0, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 6 && crm == 0 && op2 == 0) {
        vcpu->x[rt] = vcpu->far_el1;
        return 0;
    }

    /* MIDR_EL1: op0=3, op1=0, crn=0, crm=0, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 0 && crm == 0 && op2 == 0) {
        /* ARM Cortex-A72 MIDR: 0x410FD083 (matches QEMU -cpu cortex-a72) */
        vcpu->x[rt] = 0x410FD083;
        return 0;
    }

    /* ID_AA64MMFR0_EL1: op0=3, op1=0, crn=0, crm=7, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 0 && crm == 7 && op2 == 0) {
        vcpu->x[rt] = 0x0000000000001122; /* 4K/64K granule, 48-bit PA */
        return 0;
    }

    /* ID_AA64PFR0_EL1: op0=3, op1=0, crn=0, crm=4, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 0 && crm == 4 && op2 == 0) {
        vcpu->x[rt] = 0x0000000000000011; /* EL0/EL1 in AArch64, EL2/EL3 not implemented */
        return 0;
    }

    /* ID_AA64ISAR0_EL1: op0=3, op1=0, crn=0, crm=6, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 0 && crm == 6 && op2 == 0) {
        vcpu->x[rt] = 0x0000000000000000; /* No special ISA extensions */
        return 0;
    }

    /* MPIDR_EL1: op0=3, op1=0, crn=0, crm=0, op2=5 */
    if (op0 == 3 && op1 == 0 && crn == 0 && crm == 0 && op2 == 5) {
        vcpu->x[rt] = vcpu->id & 0xffffff; /* CPU affinity */
        return 0;
    }

    /* CNTFRQ_EL0: op0=3, op1=3, crn=14, crm=0, op2=0 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 0 && op2 == 0) {
        vcpu->x[rt] = vcpu->cntfrq_el0;
        return 0;
    }

    /* CNTVCT_EL0: op0=3, op1=3, crn=14, crm=0, op2=2 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 0 && op2 == 2) {
        vcpu->cntvct_el0 = (uint64_t)(clock() * (vcpu->cntfrq_el0 / (double)CLOCKS_PER_SEC));
        vcpu->x[rt] = vcpu->cntvct_el0;
        return 0;
    }

    /* CNTV_CTL_EL0: op0=3, op1=3, crn=14, crm=3, op2=1 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 3 && op2 == 1) {
        vcpu->x[rt] = vcpu->cntv_ctl_el0;
        return 0;
    }

    /* CNTV_CVAL_EL0: op0=3, op1=3, crn=14, crm=3, op2=2 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 3 && op2 == 2) {
        vcpu->x[rt] = vcpu->cntv_cval_el0;
        return 0;
    }

    /* SP_EL0: op0=3, op1=0, crn=4, crm=1, op2=0 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 1 && op2 == 0) {
        vcpu->x[rt] = vcpu->sp_el0;
        return 0;
    }

    fprintf(stderr, "tcg: unimplemented MRS op0=%d op1=%d CRn=%d CRm=%d op2=%d at PC=0x%lx\n",
            op0, op1, crn, crm, op2, (unsigned long)(vcpu->pc - 4));
    return -1;
}

static int handle_msr(tcg_vcpu_t *vcpu, uint32_t sysreg, uint64_t val)
{
    uint8_t op0 = (sysreg >> 17) & 0x3;
    uint8_t op1 = (sysreg >> 14) & 0x7;
    uint8_t crn = (sysreg >> 10) & 0xf;
    uint8_t crm = (sysreg >> 6) & 0xf;
    uint8_t op2 = (sysreg >> 3) & 0x7;

    /* SCTLR_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 1 && crm == 0 && op2 == 0) {
        vcpu->sctlr_el1 = val;
        return 0;
    }

    /* TTBR0_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 2 && crm == 0 && op2 == 0) {
        vcpu->ttbr0_el1 = val;
        return 0;
    }

    /* TTBR1_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 2 && crm == 0 && op2 == 1) {
        vcpu->ttbr1_el1 = val;
        return 0;
    }

    /* TCR_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 2 && crm == 0 && op2 == 2) {
        vcpu->tcr_el1 = val;
        return 0;
    }

    /* MAIR_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 10 && crm == 2 && op2 == 0) {
        vcpu->mair_el1 = val;
        return 0;
    }

    /* VBAR_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 12 && crm == 0 && op2 == 0) {
        vcpu->vbar_el1 = val;
        return 0;
    }

    /* ELR_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 0 && op2 == 1) {
        vcpu->elr_el1 = val;
        return 0;
    }

    /* SPSR_EL1 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 0 && op2 == 0) {
        vcpu->spsr_el1 = val;
        return 0;
    }

    /* SP_EL0 */
    if (op0 == 3 && op1 == 0 && crn == 4 && crm == 1 && op2 == 0) {
        vcpu->sp_el0 = val;
        return 0;
    }

    /* SP_EL1 */
    if (op0 == 3 && op1 == 4 && crn == 4 && crm == 1 && op2 == 0) {
        vcpu->sp_el1 = val;
        return 0;
    }

    /* CNTFRQ_EL0 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 0 && op2 == 0) {
        vcpu->cntfrq_el0 = val;
        return 0;
    }

    /* CNTV_CTL_EL0 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 3 && op2 == 1) {
        vcpu->cntv_ctl_el0 = val;
        return 0;
    }

    /* CNTV_CVAL_EL0 */
    if (op0 == 3 && op1 == 3 && crn == 14 && crm == 3 && op2 == 2) {
        vcpu->cntv_cval_el0 = val;
        vcpu->timer_irq_pending = false;
        return 0;
    }

    fprintf(stderr, "tcg: unimplemented MSR op0=%d op1=%d CRn=%d CRm=%d op2=%d val=0x%lx at PC=0x%lx\n",
            op0, op1, crn, crm, op2, (unsigned long)val, (unsigned long)(vcpu->pc - 4));
    return -1;
}


/* ══════════════════════════════════════════════════════════
 *  Main Dispatch
 * ══════════════════════════════════════════════════════════ */

int tcg_decode_exec(tcg_vcpu_t *vcpu, tcg_vm_t *vm, uint32_t insn)
{
    (void)vm; /* Used by MMU ops called indirectly */
    
    /* ── Data Processing (Immediate) ────────────────────── */
    /* Group: bits 31-23 */
    uint32_t op0 = bits(insn, 31, 23);

    switch (op0) {
    /* PC-relative address */
    case 0x10: case 0x11: { /* ADR / ADRP */
        int is_page = bit(insn, 31);
        uint8_t rd = bits(insn, 4, 0);
        int64_t imm = (sext(bits(insn, 30, 29) << 16, 2 + 16) |
                       (bits(insn, 23, 5) << 2));
        uint64_t base = is_page ? (vcpu->pc & ~0xfffULL) : vcpu->pc;
        vcpu->x[rd] = base + imm;
        return 0;
    }

    /* Add/subtract (immediate) */
    case 0x22: case 0x23: case 0x62: case 0x63:
    case 0x21: case 0x25: case 0x61: case 0x65: {
        int sf = bit(insn, 31);  /* 0=32-bit, 1=64-bit */
        int sub = bit(insn, 30);
        int setflags = bit(insn, 29);
        uint8_t rd = bits(insn, 4, 0);
        uint8_t rn = bits(insn, 9, 5);
        uint32_t imm12 = bits(insn, 21, 10);
        int shift = bits(insn, 23, 22) * 12;
        uint64_t imm = (uint64_t)imm12 << shift;
        uint64_t op_a = (rn == 31 && !setflags) ? get_sp(vcpu) : vcpu->x[rn];

        uint64_t result;
        if (sub) result = op_a - imm;
        else     result = op_a + imm;

        if (rd == 31 && !setflags) set_sp(vcpu, result);
        else vcpu->x[rd] = (sf) ? result : (uint32_t)result;

        if (setflags) {
            if (sub) set_add_flags(vcpu, op_a, ~imm + 1, result);
            else     set_add_flags(vcpu, op_a, imm, result);
        }
        return 0;
    }

    /* Logical (immediate) — AND/ORR/EOR/ANDS */
    case 0x24: case 0x64: case 0x32: case 0x72: case 0x52: { {
        int sf = bit(insn, 31);
        uint8_t rd = bits(insn, 4, 0);
        uint8_t rn = bits(insn, 9, 5);
        int opc = bits(insn, 30, 29);
        uint64_t imm = 0; /* Simplified — full bitmask decode needed for prod */

        /* For simple cases (0, all-ones) */
        uint32_t N = bit(insn, 22);
        uint32_t imms = bits(insn, 21, 10);
        uint32_t immr = bits(insn, 15, 10);

        /* Decode bitmask (simplified: only handles specific patterns) */
        if (N == 0 && imms == 0x7f && immr == 0) {
            imm = 0;
        } else if (N == 1 && imms == 0x3f && immr == 0) {
            imm = 0;
        } else {
            /* Fallback: treat as unknown — this should be improved */
            fprintf(stderr, "tcg: unimplemented logical imm at PC=0x%lx\n",
                    (unsigned long)(vcpu->pc - 4));
            return -1;
        }

        uint64_t op_a = vcpu->x[rn];
        uint64_t result;

        switch (opc) {
        case 0: result = op_a & imm; break;   /* AND */
        case 1: result = op_a | imm; break;   /* ORR */
        case 2: result = op_a ^ imm; break;   /* EOR */
        case 3: result = op_a & imm; break;   /* ANDS */
        default: return -1;
        }

        if (rd == 31 && opc != 3) set_sp(vcpu, result);
        else vcpu->x[rd] = (sf) ? result : (uint32_t)result;

        if (opc == 3) set_nz(vcpu, result);
        return 0;
    }

    /* Move wide (immediate) — MOVZ / MOVK / MOVN */
    case 0x2a: case 0x6a: {
        int sf = bit(insn, 31);
        int opc = bits(insn, 30, 29);
        int hw = bits(insn, 22, 21);
        uint16_t imm16 = bits(insn, 20, 5);
        uint8_t rd = bits(insn, 4, 0);
        uint64_t imm = (uint64_t)imm16 << (hw * 16);

        switch (opc) {
        case 0: /* MOVN */  vcpu->x[rd] = ~imm; break;
        case 2: /* MOVZ */  vcpu->x[rd] = imm; break;
        case 3: /* MOVK */  vcpu->x[rd] = (vcpu->x[rd] & ~(0xffffULL << (hw*16))) | imm; break;
        default: return -1;
        }
        if (!sf) vcpu->x[rd] = (uint32_t)vcpu->x[rd];
        return 0;
    }

    /* Bitfield — SBFM/UBFM (used for MOV, SXTB, etc.) */
    case 0x13: case 0x33: case 0x53: case 0x73: {
        int sf = bit(insn, 31);
        int opc = bits(insn, 30, 29);
        uint8_t rd = bits(insn, 4, 0);
        uint8_t rn = bits(insn, 9, 5);
        int immr = bits(insn, 21, 16);
        int imms = bits(insn, 15, 10);

        /* Detect MOV (register) alias: UBFM with immr=0, imms=size-1 */
        int size = sf ? 64 : 32;
        if (opc == 2 && immr == 0 && imms == size - 1) {
            vcpu->x[rd] = vcpu->x[rn]; /* MOV */
            return 0;
        }

        /* SBFM (signed bitfield move) */
        if (opc == 0) {
            int wmask = imms;
            uint64_t src = vcpu->x[rn];
            src = (src >> immr) & ((1ULL << (wmask + 1)) - 1);
            if (sf && (src & (1ULL << wmask))) {
                src |= ~((1ULL << (wmask + 1)) - 1);
            }
            vcpu->x[rd] = src;
            return 0;
        }

        /* UBFM — sign/zero-extend aliases (SXTB, UXTB, SXTH, UXTH, SXTW) */
        if (opc == 2) {
            uint64_t src = vcpu->x[rn];
            src = (src >> immr) & ((1ULL << (imms + 1)) - 1);
            vcpu->x[rd] = src;
            return 0;
        }

        fprintf(stderr, "tcg: unimplemented bitfield opc=%d at PC=0x%lx\n",
                opc, (unsigned long)(vcpu->pc - 4));
        return -1;
    }

    /* Extract — EXTR */
    case 0x4b: {
        /* ROR is an alias of EXTR with Rm == Rn */
        uint8_t rd = bits(insn, 4, 0);
        uint8_t rn = bits(insn, 9, 5);
        uint8_t rm = bits(insn, 20, 16);
        int imms = bits(insn, 15, 10);

        if (rn == rm) {
            /* ROR */
            uint64_t val = vcpu->x[rn];
            vcpu->x[rd] = (val >> imms) | (val << (64 - imms));
        } else {
            /* EXTR */
            uint64_t lo = vcpu->x[rn];
            uint64_t hi = vcpu->x[rm];
            vcpu->x[rd] = (lo >> imms) | (hi << (64 - imms));
        }
        return 0;
    }
    } /* end op0 switch */

    /* ── Data Processing (Register) ────────────────────── */
    /* Group: bits 28:24 */
    /* Logical shifted register (bits 28:24 = 01010) / Add/sub shifted register (01011) */
    if ((insn & 0x1f000000) == 0x0a000000 || /* AND/ORR/EOR/ANDS/etc */
        (insn & 0x1f000000) == 0x0b000000) { /* ADD/SUB/etc */

        int sf = bit(insn, 31);
        uint8_t rm = bits(insn, 20, 16);
        int shift_type = bits(insn, 23, 22);
        int shift_amt = bits(insn, 15, 10);
        uint8_t rn = bits(insn, 9, 5);
        uint8_t rd = bits(insn, 4, 0);
        int sub = bit(insn, 30);

        uint64_t op2 = vcpu->x[rm];
        switch (shift_type) {
        case 0: op2 = op2 << shift_amt; break;  /* LSL */
        case 1: op2 = op2 >> shift_amt; break;  /* LSR */
        case 2: op2 = (int64_t)op2 >> shift_amt; break; /* ASR */
        case 3: /* ROR */ op2 = (op2 >> shift_amt) | (op2 << (64 - shift_amt)); break;
        }

        uint64_t op_a = vcpu->x[rn];
        uint64_t result;

        if (sub) result = op_a - op2;
        else     result = op_a + op2;

        vcpu->x[rd] = (sf) ? result : (uint32_t)result;

        if (bit(insn, 29)) { /* S flag */
            if (sub) set_add_flags(vcpu, op_a, ~op2 + 1, result);
            else     set_add_flags(vcpu, op_a, op2, result);
        }
        return 0;
    }

    /* Logical (shifted register) — AND/ORR/EOR/BIC/etc */
    if ((insn & 0x1f000000) == 0x0a000000) { /* already handled above? */
        /* Handled above */
        return 0;
    }

    /* ── Loads and Stores ──────────────────────────────── */
    /* LDR/STR (immediate, unsigned offset) */
    /* Encoding: x1x11001_00_xxxxxxxxxx_xxxxx */
    if ((insn & 0x3f000000) == 0x39000000) {
        int size = bits(insn, 31, 30);  /* 0=byte, 1=half, 2=word, 3=double */
        int is_load = bit(insn, 22);
        uint8_t rn = bits(insn, 9, 5);
        uint8_t rt = bits(insn, 4, 0);
        uint64_t offset = (uint64_t)bits(insn, 21, 10) << size;
        uint64_t addr = ((rn == 31) ? get_sp(vcpu) : vcpu->x[rn]) + offset;

        int byte_size = 1 << size;
        uint64_t val;

        if (is_load) {
            if (tcg_mmu_resolve(vcpu, addr, byte_size, &val, false) == 1) {
                /* MMIO read — exit to machine */
                vcpu->exit.type = SV_EXIT_MMIO_READ;
                vcpu->exit.mmio_addr = addr;
                vcpu->exit.mmio_size = byte_size;
                vcpu->exit_count++;
                vcpu->mmio_exits++;
                return 1;
            }
            vcpu->x[rt] = val;
        } else {
            val = vcpu->x[rt];
            if (tcg_mmu_resolve(vcpu, addr, byte_size, &val, true) == 1) {
                /* MMIO write — exit to machine */
                vcpu->exit.type = SV_EXIT_MMIO_WRITE;
                vcpu->exit.mmio_addr = addr;
                vcpu->exit.mmio_data = val;
                vcpu->exit.mmio_size = byte_size;
                vcpu->exit_count++;
                vcpu->mmio_exits++;
                return 1;
            }
        }
        return 0;
    }

    /* LDP/STP (load/store pair, offset) */
    /* Encoding: x0x11001_0_xxxxxxxxxx_xxxxx */
    if ((insn & 0x3e000000) == 0x28000000) {
        int sf = bit(insn, 31);
        int is_load = bit(insn, 22);
        uint8_t rn = bits(insn, 9, 5);
        uint8_t rt = bits(insn, 4, 0);
        uint8_t rt2 = bits(insn, 14, 10);
        int64_t offset = sext(bits(insn, 21, 15), 7) << (sf ? 3 : 2);
        uint64_t addr = vcpu->x[rn] + offset;
        int elem_size = sf ? 8 : 4;

        if (is_load) {
            uint64_t v1, v2;
            /* If MMIO, only 4-byte accesses for now */
            if (addr >= 0x08000000 && addr < 0x20000000) {
                /* Likely device space — exit */
                vcpu->exit.type = SV_EXIT_MMIO_READ;
                vcpu->exit.mmio_addr = addr;
                vcpu->exit.mmio_size = elem_size * 2;
                vcpu->exit_count++;
                vcpu->mmio_exits++;
                return 1;
            }
            memcpy(&v1, vcpu->vm->ram + (addr - vcpu->vm->config.ram_base), elem_size);
            memcpy(&v2, vcpu->vm->ram + (addr + elem_size - vcpu->vm->config.ram_base), elem_size);
            vcpu->x[rt] = sf ? v1 : (uint32_t)v1;
            if (rt2 != 31) vcpu->x[rt2] = sf ? v2 : (uint32_t)v2;
            else set_sp(vcpu, sf ? v2 : (uint32_t)v2);
        } else {
            uint64_t v1 = vcpu->x[rt];
            uint64_t v2 = (rt2 == 31) ? get_sp(vcpu) : vcpu->x[rt2];
            memcpy(vcpu->vm->ram + (addr - vcpu->vm->config.ram_base), &v1, elem_size);
            memcpy(vcpu->vm->ram + (addr + elem_size - vcpu->vm->config.ram_base), &v2, elem_size);
        }
        return 0;
    }

    /* ── Branches ───────────────────────────────────────── */
    /* B / BL (unconditional) */
    if ((insn & 0xfc000000) == 0x14000000) {
        int is_link = bit(insn, 31);
        int64_t offset = sext(bits(insn, 25, 0), 26) * 4;
        if (is_link) vcpu->x[30] = vcpu->pc;  /* Link register = return addr */
        vcpu->pc += offset;
        return 0;
    }

    /* B.cond (conditional branch) */
    if ((insn & 0xff000010) == 0x54000000) {
        int cond = bits(insn, 3, 0);
        int64_t offset = sext(bits(insn, 23, 5), 19) * 4;
        bool taken = false;

        switch (cond >> 1) {
        case 0: taken = (cond & 1) ? (vcpu->pstate & PSTATE_Z) : !(vcpu->pstate & PSTATE_Z); break;
        case 1: taken = (cond & 1) ? (vcpu->pstate & PSTATE_C) : !(vcpu->pstate & PSTATE_C); break;
        case 2: taken = (cond & 1) ? (vcpu->pstate & PSTATE_N) : !(vcpu->pstate & PSTATE_N); break;
        case 3: taken = (cond & 1) ? (vcpu->pstate & PSTATE_V) : !(vcpu->pstate & PSTATE_V); break;
        case 4: taken = (cond & 1) ? ((vcpu->pstate & PSTATE_C) && !(vcpu->pstate & PSTATE_Z))
                                  : !((vcpu->pstate & PSTATE_C) && !(vcpu->pstate & PSTATE_Z)); break;
        case 5: taken = (cond & 1) ? ((vcpu->pstate & PSTATE_N) == (vcpu->pstate & PSTATE_V))
                                  : ((vcpu->pstate & PSTATE_N) != (vcpu->pstate & PSTATE_V)); break;
        case 6: taken = (cond & 1) ? (((vcpu->pstate & PSTATE_N) == (vcpu->pstate & PSTATE_V)) && !(vcpu->pstate & PSTATE_Z))
                                  : !(((vcpu->pstate & PSTATE_N) == (vcpu->pstate & PSTATE_V)) && !(vcpu->pstate & PSTATE_Z)); break;
        case 7: taken = true; break;
        }

        if (taken) vcpu->pc += offset;
        return 0;
    }

    /* CBNZ / CBZ */
    if ((insn & 0x7e000000) == 0x34000000) {
        int sf = bit(insn, 31);
        int is_nz = bit(insn, 24);
        int64_t offset = sext(bits(insn, 23, 5), 19) * 4;
        uint8_t rt = bits(insn, 4, 0);
        uint64_t val = sf ? vcpu->x[rt] : (uint32_t)vcpu->x[rt];

        if ((is_nz && val != 0) || (!is_nz && val == 0)) {
            vcpu->pc += offset;
        }
        return 0;
    }

    /* TBZ / TBNZ */
    if ((insn & 0x7e000000) == 0x36000000) {
        int b5 = bit(insn, 31);
        int is_nz = bit(insn, 24);
        int bit_pos = (b5 << 5) | bits(insn, 23, 19);
        int64_t offset = sext(bits(insn, 18, 5), 14) * 4;
        uint8_t rt = bits(insn, 4, 0);
        bool bit_set = (vcpu->x[rt] >> bit_pos) & 1;

        if ((is_nz && bit_set) || (!is_nz && !bit_set)) {
            vcpu->pc += offset;
        }
        return 0;
    }

    /* BR / BLR / RET */
    if ((insn & 0xfffffc1f) == 0xd61f0000) { /* BR */
        uint8_t rn = bits(insn, 9, 5);
        vcpu->pc = vcpu->x[rn];
        return 0;
    }
    if ((insn & 0xfffffc1f) == 0xd63f0000) { /* BLR */
        uint8_t rn = bits(insn, 9, 5);
        vcpu->x[30] = vcpu->pc;
        vcpu->pc = vcpu->x[rn];
        return 0;
    }
    if ((insn & 0xfffffc1f) == 0xd65f0000) { /* RET */
        uint8_t rn = bits(insn, 9, 5);
        vcpu->pc = vcpu->x[rn];
        return 0;
    }

    /* ── System Instructions ────────────────────────────── */
    /* MRS (system register to general register) */
    if ((insn & 0xfff00000) == 0xd5300000) {
        uint8_t rt = bits(insn, 4, 0);
        uint32_t sysreg = bits(insn, 19, 5);
        return handle_mrs(vcpu, sysreg, rt);
    }

    /* MSR (general register to system register) */
    if ((insn & 0xfff00000) == 0xd5100000) {
        uint8_t rt = bits(insn, 4, 0);
        uint32_t sysreg = bits(insn, 19, 5);
        return handle_msr(vcpu, sysreg, vcpu->x[rt]);
    }

    /* MSR (immediate) */
    if ((insn & 0xfff8f000) == 0xd5004000) {
        /* MSR DAIFSet / DAIFClr */
        uint8_t op = bits(insn, 8, 7);
        uint8_t imm = bits(insn, 18, 16);
        if (op == 0x3) { /* DAIFSet */
            vcpu->pstate |= (imm & 0xf) << 6;
        } else if (op == 0x2) { /* DAIFClr */
            vcpu->pstate &= ~((imm & 0xf) << 6);
        }
        return 0;
    }

    /* SVC (supervisor call) */
    if ((insn & 0xffe0001f) == 0xd4000001) {
        uint16_t svc_num = bits(insn, 20, 5);
        (void)svc_num;
        /* For now, treat as HLT — kernel init may use SVC for shutdown */
        vcpu->exit.type = SV_EXIT_HLT;
        vcpu->exit_count++;
        return 1;
    }

    /* ERET */
    if ((insn & 0xffffffff) == 0xd69f03e0) {
        vcpu->pc = vcpu->elr_el1;
        vcpu->pstate = vcpu->spsr_el1 & 0xffffffff;
        return 0;
    }

    /* ISB */
    if ((insn & 0xffffffff) == 0xd5033fdf) {
        /* Instruction Synchronization Barrier — no-op in interpreter */
        return 0;
    }

    /* DMB / DSB */
    if ((insn & 0xfffff0ff) == 0xd50330bf) { /* DMB */
        return 0; /* No-op in single-threaded interpreter */
    }
    if ((insn & 0xfffff0ff) == 0xd503309f) { /* DSB */
        return 0;
    }

    /* NOP */
    if ((insn & 0xffffffff) == 0xd503201f) {
        return 0;
    }

    /* HINT (SEV, WFE, WFI, etc.) */
    if ((insn & 0xfffff01f) == 0xd503201f) {
        uint8_t hint = bits(insn, 8, 5);
        switch (hint) {
        case 0: /* NOP */ return 0;
        case 1: /* YIELD */ return 0;
        case 2: /* WFE */
        case 3: /* WFI */
            /* WFI: wait for interrupt — for now, exit to machine */
            vcpu->exit.type = SV_EXIT_HLT;
            vcpu->exit_count++;
            return 1;
        case 4: /* SEV */ return 0;
        case 5: /* SEVL */ return 0;
        default: return 0; /* Unknown hints are NOP */
        }
    }

    /* ── Unimplemented ─────────────────────────────────── */
    fprintf(stderr, "tcg: unimplemented instruction 0x%08x at PC=0x%lx\n",
            insn, (unsigned long)(vcpu->pc - 4));
    return -1;
}
    return -1;
}

