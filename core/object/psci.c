/*
 * SiliconV — PSCI (Implementation)
 */

#include "psci.h"
#include <string.h>
#include <stdio.h>

void psci_init(psci_state_t *psci, int num_cpus)
{
    memset(psci, 0, sizeof(*psci));
    psci->num_cpus = num_cpus;
    psci->cpu_state[0] = PSCI_CPU_ONLINE;  /* CPU 0 is always online (boot CPU) */
    for (int i = 1; i < num_cpus; i++)
        psci->cpu_state[i] = PSCI_CPU_OFFLINE;
}

void psci_set_callback(psci_state_t *psci,
                       void (*cb)(int cpu, uint64_t entry, void *opaque),
                       void *opaque)
{
    psci->cpu_on_callback = cb;
    psci->opaque = opaque;
}

uint64_t psci_handle_call(psci_state_t *psci, uint64_t function_id,
                          uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    (void)arg3;

    switch (function_id) {
    case PSCI_VERSION:
        /* Return PSCI v1.1 */
        return 0x00010001;

    case PSCI_CPU_ON_64: {
        /* arg1 = target_cpu (affinity), arg2 = entry_point, arg3 = context_id */
        int cpu = arg1 & 0xFF;
        uint64_t entry = arg2;

        if (cpu < 0 || cpu >= psci->num_cpus)
            return PSCI_INVALID_PARAMS;

        if (psci->cpu_state[cpu] == PSCI_CPU_ONLINE)
            return PSCI_ALREADY_ON;

        printf("psci: CPU_ON cpu=%d entry=0x%lx\n", cpu, (unsigned long)entry);
        psci->cpu_state[cpu] = PSCI_CPU_ONLINE;

        if (psci->cpu_on_callback)
            psci->cpu_on_callback(cpu, entry, psci->opaque);

        return PSCI_SUCCESS;
    }

    case PSCI_CPU_OFF:
        /* Find calling CPU (we'd need to know which vCPU called this) */
        /* For now, mark CPU 1+ as offline */
        for (int i = 1; i < psci->num_cpus; i++) {
            if (psci->cpu_state[i] == PSCI_CPU_ONLINE) {
                psci->cpu_state[i] = PSCI_CPU_OFFLINE;
                printf("psci: CPU_OFF cpu=%d\n", i);
                break;
            }
        }
        return PSCI_SUCCESS;

    case PSCI_AFFINITY_INFO_64: {
        /* arg1 = target_affinity, arg2 = lowest_affinity_level */
        int cpu = arg1 & 0xFF;
        if (cpu < 0 || cpu >= psci->num_cpus)
            return PSCI_INVALID_PARAMS;
        return psci->cpu_state[cpu] == PSCI_CPU_OFFLINE ? 0 : 1;
    }

    case PSCI_MIGRATE_INFO_TYPE:
        /* 2 = migration not supported */
        return 2;

    case PSCI_SYSTEM_OFF:
        printf("psci: SYSTEM_OFF\n");
        psci->system_off = true;
        return PSCI_SUCCESS;

    case PSCI_SYSTEM_RESET:
        printf("psci: SYSTEM_RESET\n");
        psci->system_reset = true;
        return PSCI_SUCCESS;

    case PSCI_FEATURES:
        /* Report which functions are supported */
        switch (arg1) {
        case PSCI_VERSION:
        case PSCI_CPU_ON_64:
        case PSCI_CPU_OFF:
        case PSCI_AFFINITY_INFO_64:
        case PSCI_MIGRATE_INFO_TYPE:
        case PSCI_SYSTEM_OFF:
        case PSCI_SYSTEM_RESET:
            return PSCI_SUCCESS;
        default:
            return PSCI_NOT_SUPPORTED;
        }

    case PSCI_CPU_SUSPEND_64:
        /* We don't actually suspend — just return success */
        return PSCI_SUCCESS;

    default:
        fprintf(stderr, "psci: unknown function 0x%lx\n", (unsigned long)function_id);
        return PSCI_NOT_SUPPORTED;
    }
}
