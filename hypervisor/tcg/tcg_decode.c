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

/* ── Bitmask Immediate Decode Helpers ───────────────────── */

/* Count leading zeros in 32-bit word (returns 0..32) */
static inline int clz32(uint32_t x)
{
    if (x == 0) return 32;
    int n = 0;
    if ((x & 0xFFFF0000) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000) == 0) { n +=  8; x <<=  8; }
    if ((x & 0xF0000000) == 0) { n +=  4; x <<=  4; }
    if ((x & 0xC0000000) == 0) { n +=  2; x <<=  2; }
    if ((x & 0x80000000) == 0) { n +=  1; x <<=  1; }
    return n;
}

/* Right-rotate 'val' by 'amt' within an 'size'-bit window */
static inline uint64_t ror64(uint64_t val, unsigned amt, unsigned size)
{
    if (amt == 0 || size <= 1) return val;
    uint64_t mask = (size == 64) ? ~0ULL : ((1ULL << size) - 1);
    val &= mask;
    return ((val >> amt) | (val << (size - amt))) & mask;
}

/* Validate whether N:immr:imms is a valid logical immediate encoding.
 * Returns 1 if valid, 0 if invalid. sf=0 for 32-bit (W), sf=1 for 64-bit (X). */
static inline int logical_imm_valid(uint32_t N, uint32_t immr,
                                    uint32_t imms, uint32_t sf)
{
    (void)immr; /* immr does not affect validity */
    unsigned reg_size = sf ? 64 : 32;
    if (reg_size == 32 && N != 0)
        return 0; /* N must be 0 in 32-bit mode */

    uint32_t value = (N << 6) | (~imms & 0x3f);
    int lz = clz32(value);
    if (lz == 32)
        return 0; /* value == 0 => invalid */

    unsigned size = 1U << (31 - lz);
    unsigned S = imms & (size - 1);
    if (S == size - 1)
        return 0; /* element would be all-ones (unrepresentable) */

    return 1;
}

/* Decode N:immr:imms into the full uint64_t bitmask value.
 * Only call if logical_imm_valid() returns 1. */
static inline uint64_t logical_imm_decode(uint32_t N, uint32_t immr,
                                          uint32_t imms, uint32_t sf)
{
    unsigned reg_size = sf ? 64 : 32;

    /* Determine element size */
    uint32_t value = (N << 6) | (~imms & 0x3f);
    int lz = clz32(value);
    unsigned size = 1U << (31 - lz);

    /* Extract rotation and run-length within the element */
    unsigned R = immr & (size - 1);
    unsigned S = imms & (size - 1);

    /* Build element: S+1 consecutive ones */
    uint64_t pattern = (1ULL << (S + 1)) - 1;

    /* Right-rotate within the element */
    pattern = ror64(pattern, R, size);

    /* Replicate to fill the register */
    while (size < reg_size) {
        pattern |= (pattern << size);
        size <<= 1;
    }

    if (reg_size == 32)
        pattern &= 0xFFFFFFFFULL;

    return pattern;
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

/* ── Safe register access ──────────────────────────────────── */
/* x[0..30] = X0-X30, reg=31 → XZR (read as 0, write discarded) */
static inline uint64_t get_xzr(tcg_vcpu_t *vcpu, unsigned reg) {
    return (reg < 31) ? vcpu->x[reg] : 0;
}
static inline void set_xzr(tcg_vcpu_t *vcpu, unsigned reg, uint64_t val) {
    if (reg < 31) vcpu->x[reg] = val;
    /* reg=31 → XZR, write discarded */
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

    /* Unknown system register: return 0 (safe for boot) */
#if SV_DEBUG
    fprintf(stderr, "tcg: MRS unknown sysreg op0=%d op1=%d CRn=%d CRm=%d op2=%d at PC=0x%lx (returns 0)\n",
            op0, op1, crn, crm, op2, (unsigned long)(vcpu->pc - 4));
#endif
    return 0;
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

    /* Unknown system register: silently ignore write (safe for boot) */
#if SV_DEBUG
    fprintf(stderr, "tcg: MSR unknown sysreg op0=%d op1=%d CRn=%d CRm=%d op2=%d val=0x%lx at PC=0x%lx (ignored)\n",
            op0, op1, crn, crm, op2, (unsigned long)val, (unsigned long)(vcpu->pc - 4));
#endif
    return 0;
}


/* ══════════════════════════════════════════════════════════
 *  Main Dispatch
 * ══════════════════════════════════════════════════════════ */

int tcg_decode_exec(tcg_vcpu_t *vcpu, tcg_vm_t *vm, uint32_t insn)
{
    /* ── Short instruction trace for boot debugging ── */
    if (vcpu->insn_count <= 120) {
        fprintf(stderr, "tcg: [%lu] 0x%08x at PC=0x%lx\n",
                (unsigned long)vcpu->insn_count, insn,
                (unsigned long)(vcpu->pc - 4));
    }

    (void)vm; /* Used by MMU ops called indirectly */
    
    /* ── Data Processing (Immediate) ────────────────────── */
    /* DP-Immediate encoding spans four nibble values based on sf:op:S:bit28:
     *   immlo=0:     immlo=1:
     *     ADRP  (0x9)  ADRP  (0xB)  — bit29 is the immlo field, not opcode
     *     ADD   (0x9)  ADDS  (0xB)  — S flag
     *     SUB   (0xD)  SUBS  (0xF)  — S flag
     *     MOVN  (0x9)  MOVK  (0xF)  — MOVZ at (0xD)
     *     AND   (0x9)  EOR   (0xB)  — ORR at (0x9), ANDS at (0xB)
     * ADR lives at bits[31:28] = 0001/0011 (0x1x/0x3x) and is excluded here.
     * Non-DP-Imm instructions that share these nibbles have dp_grp outside
     * 0x20-0x27 and fall through the switch harmlessly.
     */
    uint32_t top_nibble = bits(insn, 31, 28);
    if (top_nibble == 0x9 || top_nibble == 0xB || top_nibble == 0xD || top_nibble == 0xF) {
    /* Use bits[28:23] (6 bits) for clean group dispatch:
     *   0x20-0x21 = ADRP     (PC-relative, page)
     *   0x22-0x23 = Add/sub  (immediate, shift=0 or 12)
     *   0x24, 0x26 = Logical / Bitfield (bit23=0, 32/64-bit variants)
     *   0x25, 0x27 = Move Wide (bit23=1, 32/64-bit variants)
     */
    uint32_t dp_grp = bits(insn, 28, 23);

    switch (dp_grp) {

    /* ── ADRP (PC-relative, page) ────────────────────────── */
    /* dp_grp=0x20 corresponds to ADRP with bit23=0,
     * dp_grp=0x21 corresponds to ADRP with bit23=1.
     * ADR has bits 31-29 = 00x (top nibble 0x1/0x3), excluded by guard.
     * 
     * The 21-bit signed page offset is split across the encoding:
     *   immlo = bits[30:29] — LOW 2 bits (bit29 varies per instruction)
     *   immhi = bits[23:5]  — HIGH 19 bits
     *   imm   = SignExtend(immhi:immlo, 21)
     *   Xd    = page(PC) + (imm << 12) */
    case 0x20: case 0x21: {
        uint8_t rd = bits(insn, 4, 0);
        int64_t imm = ((int64_t)bits(insn, 23, 5) << 2) |
                      (int64_t)bits(insn, 30, 29);
        /* PC has been pre-incremented by +4 in tcg_step, so use (pc-4) */
        uint64_t insn_addr = vcpu->pc - 4;
        vcpu->x[rd] = (insn_addr & ~0xfffULL) + (sext(imm, 21) << 12);
        return 0;
    }

    /* ── Add/sub (immediate) ─────────────────────────────── */
    /* All 8 variants: ADD/SUB/ADDS/SUBS × 32/64-bit
     * dp_grp=0x22: shift=0, dp_grp=0x23: shift=12 */
    case 0x22: case 0x23: {
        int sf = bit(insn, 31);
        int sub = bit(insn, 30);
        int setflags = bit(insn, 29);
        uint8_t rd = bits(insn, 4, 0);
        uint8_t rn = bits(insn, 9, 5);
        uint32_t imm12 = bits(insn, 21, 10);
        int shift = (dp_grp == 0x23) ? 12 : 0;
        uint64_t imm = (uint64_t)imm12 << shift;
        uint64_t op_a = (rn == 31) ? get_sp(vcpu) : vcpu->x[rn];
        uint64_t result;

        if (sub) result = op_a - imm;
        else     result = op_a + imm;

        /* Rd=31: if S=0 write to SP, if S=1 discard (ANDS only sets flags) */
        if (rd == 31) {
            if (!setflags) set_sp(vcpu, (sf) ? result : (uint32_t)result);
        } else {
            vcpu->x[rd] = (sf) ? result : (uint32_t)result;
        }

        if (setflags) {
            if (sub) set_add_flags(vcpu, op_a, ~imm + 1, result);
            else     set_add_flags(vcpu, op_a, imm, result);
        }
        return 0;
    }

    /* ── Logical / Bitfield (bits[28:23]=10010x, bit23=0) ──── */
    /* dp_grp=0x24 (32-bit) or 0x26 (64-bit).
     * ARMv8 priority: Bitfield (bit21=1) → Logical immediate (valid N:immr:imms).
     * Move Wide does NOT share this encoding space (it uses bit23=1). */
    case 0x24: case 0x26: {
        int sf = bit(insn, 31);
        int opc = bits(insn, 30, 29);
        uint8_t rd = bits(insn, 4, 0);
        uint8_t rn = bits(insn, 9, 5);
        uint32_t N = bit(insn, 22);
        uint32_t immr6 = bits(insn, 21, 16);
        uint32_t imms6 = bits(insn, 15, 10);

        /* ── (1) Bitfield: SBFM/BFM/UBFM ───────────────────── */
        /* Bitfield has bit21=1 FIXED. opc=00=SBFM, 01=BFM, 10=UBFM.
         * opc=11 (ANDS) never matches Bitfield. */
        if (bit(insn, 21) && imms6 < (unsigned)(sf ? 64 : 32) && opc != 3) {
            int immr = bits(insn, 20, 16);
            int imms = (int)imms6;

            /* MOV alias: UBFM immr=0, imms=size-1 → MOV Xd, Xn */
            if (opc == 2 && immr == 0 && imms == (sf ? 63 : 31)) {
                set_xzr(vcpu, rd, get_xzr(vcpu, rn));
                return 0;
            }

            /* SBFM */
            if (opc == 0) {
                uint64_t src = get_xzr(vcpu, rn);
                src = (src >> immr) & ((1ULL << (imms + 1)) - 1);
                if (sf && (src & (1ULL << imms))) {
                    src |= ~((1ULL << (imms + 1)) - 1);
                }
                set_xzr(vcpu, rd, src);
                return 0;
            }

            /* UBFM */
            if (opc == 2) {
                uint64_t src = get_xzr(vcpu, rn);
                src = (src >> immr) & ((1ULL << (imms + 1)) - 1);
                set_xzr(vcpu, rd, src);
                return 0;
            }

            /* BFM (opc=1) — Bitfield Move */
            if (opc == 1) {
                uint64_t src = get_xzr(vcpu, rn);
                uint64_t dst = get_xzr(vcpu, rd);
                uint64_t mask = ((1ULL << (imms + 1)) - 1);
                uint64_t field = (src >> immr) & mask;
                set_xzr(vcpu, rd, (dst & ~mask) | field);
                return 0;
            }
        }

        /* ── (2) Logical (immediate): AND/ORR/EOR/ANDS ─────── */
        if (logical_imm_valid(N, immr6, imms6, sf)) {
            uint64_t imm = logical_imm_decode(N, immr6, imms6, sf);
            uint64_t op_a = get_xzr(vcpu, rn);  /* Rn=31 → XZR */
            uint64_t result;

            switch (opc) {
            case 0: result = op_a & imm; break;   /* AND */
            case 1: result = op_a | imm; break;   /* ORR */
            case 2: result = op_a ^ imm; break;   /* EOR */
            case 3: result = op_a & imm; break;   /* ANDS */
            default: return -1;
            }

            if (!sf) result = (uint32_t)result;

            if (rd == 31) {
                if (opc == 3) {
                    /* ANDS: only set flags, discard result (XZR) */
                    set_nz(vcpu, result);
                } else {
                    /* AND/ORR/EOR: write to SP */
                    set_sp(vcpu, result);
                }
            } else {
                vcpu->x[rd] = result;
            }

            if (opc == 3) set_nz(vcpu, result);
            return 0;
        }

        /* ── (3) Neither Bitfield nor Logical → unallocated ── */
        fprintf(stderr, "tcg: unallocated Logical/Bitfield encoding at PC=0x%lx "
                "(N=%u immr=%u imms=%u opc=%u)\n",
                (unsigned long)(vcpu->pc - 4), N, immr6, imms6, opc);
        return -1;
    }

    /* ── Move Wide (bits[28:23]=10010x, bit23=1) ──────────── */
    /* dp_grp=0x25 (32-bit) or 0x27 (64-bit).
     * MOVN/MOVZ/MOVK only — no encoding ambiguity with Logical/Bitfield.
     * The ARM ARM confirms this encoding space is architecturally distinct.
     * For Move Wide, Rd=31 → SP (not XZR). */
    case 0x25: case 0x27: {
        int sf = bit(insn, 31);
        int opc = bits(insn, 30, 29);
        uint8_t rd = bits(insn, 4, 0);
        int hw = bits(insn, 22, 21);
        uint16_t imm16 = bits(insn, 20, 5);
        uint64_t imm = (uint64_t)imm16 << (hw * 16);

        /* opc=01 is not a valid Move Wide encoding */
        if (opc == 1) {
            fprintf(stderr, "tcg: invalid Move Wide opc=01 at PC=0x%lx\n",
                    (unsigned long)(vcpu->pc - 4));
            return -1;
        }

        uint64_t old_val = (rd == 31) ? get_sp(vcpu) : vcpu->x[rd];
        uint64_t new_val;

        switch (opc) {
        case 0: new_val = ~imm; break;                                    /* MOVN */
        case 2: new_val = imm; break;                                     /* MOVZ */
        case 3: new_val = (old_val & ~(0xffffULL << (hw*16))) | imm; break; /* MOVK */
        default: return -1;
        }

        if (!sf) new_val = (uint32_t)new_val;

        if (rd == 31) set_sp(vcpu, new_val);
        else          vcpu->x[rd] = new_val;
        return 0;
    }

    } /* end dp_grp switch */
    } /* end if (op0 == DP-Immediate) */

    /* ── Short instruction trace for boot debugging ── */
    if (vcpu->insn_count <= 30) {
        fprintf(stderr, "tcg: [%lu] 0x%08x at PC=0x%lx\n",
                (unsigned long)vcpu->insn_count, insn,
                (unsigned long)(vcpu->pc - 4));
    }

    /* ── Add/Sub (shifted register) ────────────────────── */
    /* bits 28-24 = 01011 (sf=0) or 11011 (sf=1). Mask 0x1F catches both. */
    if ((insn & 0x1f000000) == 0x0b000000) {

        int sf = bit(insn, 31);
        int sub = bit(insn, 30);          /* 0=ADD, 1=SUB */
        int setflags = bit(insn, 29);     /* S flag */
        uint8_t rm = bits(insn, 20, 16);
        int shift_type = bits(insn, 23, 22);
        int shift_amt = bits(insn, 15, 10);
        uint8_t rn = bits(insn, 9, 5);
        uint8_t rd = bits(insn, 4, 0);

        /* Rm=31 → XZR (zero) */
        uint64_t op2 = (rm == 31) ? 0 : vcpu->x[rm];
        switch (shift_type) {
        case 0: op2 = op2 << shift_amt; break;
        case 1: op2 = op2 >> shift_amt; break;
        case 2: op2 = (int64_t)op2 >> shift_amt; break;
        case 3: op2 = (op2 >> shift_amt) | (op2 << (64 - shift_amt)); break;
        }

        /* Rn=31 → SP */
        uint64_t op_a = (rn == 31) ? get_sp(vcpu) : vcpu->x[rn];
        uint64_t result;

        if (sub) result = op_a - op2;
        else     result = op_a + op2;

        /* Rd=31: if S=0 write to SP; if S=1 discard (only set flags) */
        if (rd == 31) {
            if (!setflags) set_sp(vcpu, (sf) ? result : (uint32_t)result);
        } else {
            vcpu->x[rd] = (sf) ? result : (uint32_t)result;
        }

        if (setflags) {
            if (sub) set_add_flags(vcpu, op_a, ~op2 + 1, result);
            else     set_add_flags(vcpu, op_a, op2, result);
        }
        return 0;
    }

    /* ── Logical (shifted register) ────────────────────── */
    /* bits 28-24 = 01010 (sf=0) or 11010 (sf=1). Mask 0x1F catches both.
     * AND/ORR/EOR/ANDS + BIC/ORN/EON/BICS (N=1 inverts second operand). */
    if ((insn & 0x1f000000) == 0x0a000000) {

        int sf = bit(insn, 31);
        int opc = bits(insn, 30, 29);     /* 00=AND, 01=ORR, 10=EOR, 11=ANDS */
        int invert = bit(insn, 21);       /* N=1 → BIC/ORN/EON/BICS */
        uint8_t rm = bits(insn, 20, 16);
        int shift_type = bits(insn, 23, 22);
        int shift_amt = bits(insn, 15, 10);
        uint8_t rn = bits(insn, 9, 5);
        uint8_t rd = bits(insn, 4, 0);

        /* Rm=31 → XZR (zero); Rn=31 → XZR (zero) */
        uint64_t op2 = (rm == 31) ? 0 : vcpu->x[rm];
        switch (shift_type) {
        case 0: op2 = op2 << shift_amt; break;
        case 1: op2 = op2 >> shift_amt; break;
        case 2: op2 = (int64_t)op2 >> shift_amt; break;
        case 3: op2 = (op2 >> shift_amt) | (op2 << (64 - shift_amt)); break;
        }

        if (invert) op2 = ~op2;

        uint64_t op_a = (rn == 31) ? 0 : vcpu->x[rn];
        uint64_t result;

        switch (opc) {
        case 0: result = op_a & op2; break;   /* AND / BIC */
        case 1: result = op_a | op2; break;   /* ORR / ORN */
        case 2: result = op_a ^ op2; break;   /* EOR / EON */
        case 3: result = op_a & op2; break;   /* ANDS / BICS */
        default: return -1;
        }

        if (!sf) result = (uint32_t)result;

        if (rd == 31) {
            if (opc == 3) {
                /* ANDS: only set flags, discard result (XZR) */
                set_nz(vcpu, result);
            } else {
                /* AND/ORR/EOR: write to SP */
                set_sp(vcpu, result);
            }
        } else {
            vcpu->x[rd] = result;
        }

        if (opc == 3) set_nz(vcpu, result);  /* ANDS/BICS sets NZ */

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
            /* LDR XZR → discard result */
            if (rt != 31) vcpu->x[rt] = val;
        } else {
            /* STR XZR → read 0 (XZR = zero register) */
            val = (rt == 31) ? 0 : vcpu->x[rt];
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
        /* LDP/STP: Rn=31 → SP */
        uint64_t addr = ((rn == 31) ? get_sp(vcpu) : vcpu->x[rn]) + offset;
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
            /* LDP: Rt=31 → SP, Rt2=31 → SP (stack pointer) */
            if (rt == 31) set_sp(vcpu, sf ? v1 : (uint32_t)v1);
            else          vcpu->x[rt] = sf ? v1 : (uint32_t)v1;
            if (rt2 == 31) set_sp(vcpu, sf ? v2 : (uint32_t)v2);
            else           vcpu->x[rt2] = sf ? v2 : (uint32_t)v2;
        } else {
            /* STP: Rt=31 → XZR (store 0), Rt2=31 → XZR */
            uint64_t v1 = (rt == 31) ? 0 : vcpu->x[rt];
            uint64_t v2 = (rt2 == 31) ? 0 : vcpu->x[rt2];
            memcpy(vcpu->vm->ram + (addr - vcpu->vm->config.ram_base), &v1, elem_size);
            memcpy(vcpu->vm->ram + (addr + elem_size - vcpu->vm->config.ram_base), &v2, elem_size);
        }
        return 0;
    }

    /* ── Branches ───────────────────────────────────────── */
    /* B / BL (unconditional) — bits[31:26] = xx0101 (x=0 B, x=1 BL) */
    if ((insn & 0x7c000000) == 0x14000000) {
        int is_link = bit(insn, 31);
        /* PC has been incremented by +4 in the fetch step (tcg_step).
         * The ARM offset is relative to the instruction's own address,
         * so we must subtract 4 from vcpu->pc to get the original PC. */
        uint64_t base = vcpu->pc - 4;
        int64_t offset = sext(bits(insn, 25, 0), 26) * 4;
        if (is_link) vcpu->x[30] = vcpu->pc;  /* X30 = return addr (next insn) */
        vcpu->pc = base + offset;
        return 0;
    }

    /* B.cond (conditional branch) */
    if ((insn & 0xff000010) == 0x54000000) {
        int cond = bits(insn, 3, 0);
        uint64_t base = vcpu->pc - 4; /* PC already advanced past this insn */
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

        if (taken) vcpu->pc = base + offset;
        return 0;
    }

    /* CBNZ / CBZ */
    if ((insn & 0x7e000000) == 0x34000000) {
        int sf = bit(insn, 31);
        int is_nz = bit(insn, 24);
        uint64_t base = vcpu->pc - 4; /* PC already advanced past this insn */
        int64_t offset = sext(bits(insn, 23, 5), 19) * 4;
        uint8_t rt = bits(insn, 4, 0);
        /* Rt=31 → XZR (zero) */
        uint64_t val = get_xzr(vcpu, rt);
        if (!sf) val = (uint32_t)val;

        if ((is_nz && val != 0) || (!is_nz && val == 0)) {
            vcpu->pc = base + offset;
        }
        return 0;
    }

    /* TBZ / TBNZ */
    if ((insn & 0x7e000000) == 0x36000000) {
        int b5 = bit(insn, 31);
        int is_nz = bit(insn, 24);
        uint64_t base = vcpu->pc - 4; /* PC already advanced past this insn */
        int bit_pos = (b5 << 5) | bits(insn, 23, 19);
        int64_t offset = sext(bits(insn, 18, 5), 14) * 4;
        uint8_t rt = bits(insn, 4, 0);
        /* Rt=31 → XZR (zero) */
        uint64_t rt_val = get_xzr(vcpu, rt);
        bool bit_set = (rt_val >> bit_pos) & 1;

        if ((is_nz && bit_set) || (!is_nz && !bit_set)) {
            vcpu->pc = base + offset;
        }
        return 0;
    }

    /* BR / BLR / RET */
    if ((insn & 0xfffffc1f) == 0xd61f0000) { /* BR */
        uint8_t rn = bits(insn, 9, 5);
        /* Rn=31 → XZR (BR XZR = jump to 0) */
        uint64_t target = get_xzr(vcpu, rn);
        fprintf(stderr, "tcg: BR X%d = 0x%lx at PC=0x%lx (insn_count=%lu)\n",
                rn, (unsigned long)target,
                (unsigned long)(vcpu->pc - 4), (unsigned long)vcpu->insn_count);
        vcpu->pc = target;
        return 0;
    }
    if ((insn & 0xfffffc1f) == 0xd63f0000) { /* BLR */
        uint8_t rn = bits(insn, 9, 5);
        /* Rn=31 → XZR (BLR XZR = jump to 0) */
        uint64_t target = get_xzr(vcpu, rn);
        fprintf(stderr, "tcg: BLR X%d = 0x%lx LR=0x%lx at PC=0x%lx (insn_count=%lu)\n",
                rn, (unsigned long)target, (unsigned long)vcpu->pc,
                (unsigned long)(vcpu->pc - 4), (unsigned long)vcpu->insn_count);
        vcpu->x[30] = vcpu->pc;
        vcpu->pc = target;
        return 0;
    }
    if ((insn & 0xfffffc1f) == 0xd65f0000) { /* RET */
        uint8_t rn = bits(insn, 9, 5);
        /* Rn=31 → XZR (RET XZR = jump to 0) */
        uint64_t target = get_xzr(vcpu, rn);
        fprintf(stderr, "tcg: RET X%d = 0x%lx at PC=0x%lx (insn_count=%lu)\n",
                rn, (unsigned long)target,
                (unsigned long)(vcpu->pc - 4), (unsigned long)vcpu->insn_count);
        vcpu->pc = target;
        return 0;
    }

    /* ── System Instructions ────────────────────────────── */
    /* MRS (system register to general register) */
    if ((insn & 0xfff00000) == 0xd5300000) {
        uint8_t rt = bits(insn, 4, 0);
        uint32_t sysreg = bits(insn, 19, 5);
        /* MRS XZR: discard result (write to XZR is a NOP).
         * MUST short-circuit here because handle_mrs writes to
         * vcpu->x[rt] which is OUT OF BOUNDS for rt=31 and
         * would corrupt vcpu->pc (which shares the same memory). */
        if (rt == 31) return 0;
        return handle_mrs(vcpu, sysreg, rt);
    }

    /* MSR (general register to system register) */
    if ((insn & 0xfff00000) == 0xd5100000) {
        uint8_t rt = bits(insn, 4, 0);
        uint32_t sysreg = bits(insn, 19, 5);
        /* MSR XZR, sysreg → write 0 to system register */
        return handle_msr(vcpu, sysreg, get_xzr(vcpu, rt));
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
    fprintf(stderr, "tcg: unimplemented instr 0x%08x at PC=0x%lx (insn_count=%lu)\n",
            insn, (unsigned long)(vcpu->pc - 4), (unsigned long)vcpu->insn_count);
    return -1;
}

