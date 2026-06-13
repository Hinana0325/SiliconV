/*
 * SiliconV — TCG Internal Headers
 *
 * These stubs will be filled in as the implementation matures.
 * For now, they define the interfaces used by tcg.c and tcg_exec.c.
 */

#ifndef SILICONV_TCG_DECODE_H
#define SILICONV_TCG_DECODE_H

#include "tcg.h"

/* Main decode + execute entry point (defined in tcg_decode.c) */
int tcg_decode_exec(tcg_vcpu_t *vcpu, tcg_vm_t *vm, uint32_t insn);

#endif /* SILICONV_TCG_DECODE_H */
