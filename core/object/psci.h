/*
 * SiliconV — PSCI (Power State Coordination Interface)
 *
 * Handles CPU lifecycle via HVC/SMC calls from the guest.
 * Required for multi-core Linux boot.
 *
 * Reference: ARM PSCI Specification v1.1
 */

#ifndef SILICONV_PSCI_H
#define SILICONV_PSCI_H

#include <stdint.h>
#include <stdbool.h>

/* ── PSCI Function IDs ─────────────────────────── */
#define PSCI_VERSION             0x84000000
#define PSCI_CPU_SUSPEND_64     0xC4000001
#define PSCI_CPU_OFF            0x84000002
#define PSCI_CPU_ON_64          0xC4000003
#define PSCI_AFFINITY_INFO_64   0xC4000004
#define PSCI_MIGRATE_INFO_TYPE  0x84000006
#define PSCI_SYSTEM_OFF         0x84000008
#define PSCI_SYSTEM_RESET       0x84000009
#define PSCI_FEATURES           0x8400000A

/* ── PSCI Return Codes ─────────────────────────── */
#define PSCI_SUCCESS            0
#define PSCI_NOT_SUPPORTED      (-1)
#define PSCI_INVALID_PARAMS     (-2)
#define PSCI_DENIED             (-3)
#define PSCI_ALREADY_ON         (-4)
#define PSCI_ON_PENDING         (-5)
#define PSCI_INTERNAL_FAILURE   (-6)

/* ── CPU States ────────────────────────────────── */
typedef enum {
    PSCI_CPU_OFFLINE = 0,
    PSCI_CPU_ONLINE,
    PSCI_CPU_SUSPENDED,
} psci_cpu_state_t;

/* ── PSCI State ────────────────────────────────── */
typedef struct {
    psci_cpu_state_t cpu_state[8];
    int num_cpus;
    bool system_off;
    bool system_reset;

    /* Callback to actually bring up a CPU */
    void (*cpu_on_callback)(int cpu, uint64_t entry, void *opaque);
    void *opaque;
} psci_state_t;

/* ── API ───────────────────────────────────────── */

void psci_init(psci_state_t *psci, int num_cpus);

void psci_set_callback(psci_state_t *psci,
                       void (*cb)(int cpu, uint64_t entry, void *opaque),
                       void *opaque);

/* Handle a PSCI call (from HVC/SMC).
 * function_id in x0, args in x1-x3.
 * Returns result in x0. */
uint64_t psci_handle_call(psci_state_t *psci, uint64_t function_id,
                          uint64_t arg1, uint64_t arg2, uint64_t arg3);

#endif /* SILICONV_PSCI_H */
