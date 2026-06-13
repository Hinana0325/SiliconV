/*
 * SiliconV — TCG Execution Loop (Internal)
 */
#ifndef SILICONV_TCG_EXEC_H
#define SILICONV_TCG_EXEC_H

#include "tcg.h"

int tcg_vcpu_run(sv_vcpu_t *vcpu, sv_vcpu_exit_t *exit);

#endif /* SILICONV_TCG_EXEC_H */
