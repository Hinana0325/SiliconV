/*
 * SiliconV — Android Device Tree Additions
 *
 * Additional DTB nodes required for Android.
 * Merge these into the main DTB generator.
 *
 * These are NOT hardware devices — they're kernel subsystem
 * configurations that Android expects to find in the device tree.
 */

#ifndef SILICONV_ANDROID_DTB_H
#define SILICONV_ANDROID_DTB_H

#include <stdint.h>

/* ── Android DTB Config ────────────────────────── */
typedef struct {
    bool     binder_enabled;
    bool     ashmem_enabled;
    bool     ion_enabled;
    uint64_t cma_base;       /* CMA region base (guest physical) */
    uint64_t cma_size;       /* CMA region size */
    char     hardware_id[32]; /* androidboot.hardware value */
    int      selinux_mode;   /* 0=disabled, 1=permissive, 2=enforcing */
} sv_android_dtb_config_t;

static inline sv_android_dtb_config_t sv_android_dtb_default(void)
{
    sv_android_dtb_config_t cfg = {
        .binder_enabled = true,
        .ashmem_enabled = true,
        .ion_enabled = true,
        .cma_base = 0x440000000ULL,   /* 17G — after guest RAM */
        .cma_size = 256 * 1024 * 1024, /* 256MB */
        .selinux_mode = 1,            /* Permissive by default */
    };
    snprintf(cfg.hardware_id, sizeof(cfg.hardware_id), "siliconv");
    return cfg;
}

/*
 * DTB additions for Android (written to /chosen):
 *
 * chosen {
 *     bootargs = "... androidboot.hardware=siliconv
 *                  androidboot.selinux=permissive
 *                  androidboot.first_stage_console=1";
 * };
 *
 * Reserved memory for CMA:
 * reserved-memory {
 *     linux,cma {
 *         compatible = "shared-dma-pool";
 *         reusable;
 *         size = <0x0 0x10000000>;  // 256MB
 *         linux,cma-default;
 *     };
 * };
 *
 * No DTB nodes needed for binder/ashmem — they're kernel-internal.
 */

#endif /* SILICONV_ANDROID_DTB_H */
