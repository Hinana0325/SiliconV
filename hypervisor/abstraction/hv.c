/*
 * SiliconV — Hypervisor Abstraction (Backend Registry)
 */

#include "hv.h"
#include <string.h>
#include <stdio.h>

#define MAX_BACKENDS 4

static const sv_hv_ops_t *backends[MAX_BACKENDS];
static int num_backends = 0;

int sv_hv_register(const sv_hv_ops_t *ops)
{
    if (num_backends >= MAX_BACKENDS) {
        fprintf(stderr, "sv: too many hypervisor backends\n");
        return -1;
    }
    backends[num_backends++] = ops;
    return 0;
}

const sv_hv_ops_t* sv_hv_get_best(void)
{
    /* Prefer KVM > HVF > WHPX */
    for (int i = 0; i < num_backends; i++) {
        if (backends[i]->type == SV_HV_KVM) return backends[i];
    }
    for (int i = 0; i < num_backends; i++) {
        if (backends[i]->type == SV_HV_HVF) return backends[i];
    }
    for (int i = 0; i < num_backends; i++) {
        if (backends[i]->type == SV_HV_WHPX) return backends[i];
    }
    return NULL;
}

sv_vm_config_t sv_vm_config_default(void)
{
    sv_vm_config_t config = {
        .num_cpus = 4,
        .ram_size = 4ULL * 1024 * 1024 * 1024,  /* 4 GB */
        .ram_base = 0x400000000ULL,              /* 16 GB */
        .kernel_path = NULL,
        .dtb_path = NULL,
        .initrd_path = NULL,
        .cmdline = "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw",
    };
    return config;
}
