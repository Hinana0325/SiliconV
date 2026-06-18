/*
 * SiliconV — XNU Boot Loader
 *
 * Ties together IMG4 parsing, Mach-O loading, DeviceTree handling,
 * and BootArgs setup to boot Apple XNU kernels.
 *
 * This replaces iBoot's role in the boot chain — it loads the kernel
 * directly without emulating the actual Apple boot firmware.
 */

#ifndef SILICONV_XNU_BOOT_H
#define SILICONV_XNU_BOOT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── XNU Boot Configuration ─────────────────────── */
typedef struct {
    /* Kernel image path (IMG4 container or raw Mach-O) */
    const char *kernel_path;

    /* Optional: separate DTB path (overrides DTB in IMG4) */
    const char *dtb_path;

    /* Optional: ramdisk path */
    const char *ramdisk_path;

    /* Optional: TrustCache path */
    const char *trustcache_path;

    /* Command line (boot-args) */
    const char *cmdline;

    /* Guest memory parameters */
    uint64_t    ram_base;
    uint64_t    ram_size;

    /* Video parameters (for bootargs) */
    uint32_t    video_width;
    uint32_t    video_height;
    uint32_t    video_depth;

    /* Boot flags */
    uint32_t    boot_flags;

    /* KASLR slide (0 = auto, ~0 = disable) */
    uint64_t    aslr_slide;

    /* BootArgs revision (0 = auto-detect) */
    int         bootargs_revision;
} xnu_boot_config_t;

/* ── XNU Boot State ────────────────────────────── */
typedef struct {
    xnu_boot_config_t config;

    /* Loaded data */
    uint8_t    *kernel_data;
    size_t      kernel_size;

    uint8_t    *dtb_data;
    size_t      dtb_size;

    uint8_t    *ramdisk_data;
    size_t      ramdisk_size;

    uint8_t    *trustcache_data;
    size_t      trustcache_size;

    /* Parsed structures */
    struct {
        uint64_t entry_point;       /* Physical entry address (guest phys) */
        uint64_t entry_point_virt;  /* Virtual entry address (for vCPU PC = 0xFFFFFFF0...) */
        uint64_t virt_base;         /* Kernel virtual base address (linked address) */
        uint64_t dtb_addr;          /* Physical DTB address */
        uint64_t bootargs_addr;     /* Physical BootArgs address */
        uint64_t aslr_slide;        /* Applied ASLR offset */
        bool     is_fileset;        /* MH_FILESET kernel */
    } boot_info;
} xnu_boot_state_t;

/* ── API ───────────────────────────────────────── */

/* Initialize XNU boot state */
void xnu_boot_init(xnu_boot_state_t *state, const xnu_boot_config_t *config);

/* Load kernel from file
 * Returns 0 on success, -1 on error. */
int xnu_boot_load_kernel(xnu_boot_state_t *state, const uint8_t *data, size_t size);

/* Load DTB from file
 * Returns 0 on success, -1 on error. */
int xnu_boot_load_dtb(xnu_boot_state_t *state, const uint8_t *data, size_t size);

/* Setup the entire boot environment in guest RAM.
 * This performs:
 *   1. Kernel segment loading
 *   2. DTB population and serialization
 *   3. BootArgs setup
 *   4. TrustCache loading (optional)
 *   5. RAM disk loading (optional)
 *
 * Returns 0 on success, -1 on error. */
int xnu_boot_setup(xnu_boot_state_t *state,
                    uint8_t *guest_ram, uint64_t ram_base, uint64_t ram_size);

/* Get the kernel entry point (for setting vCPU PC — physical address) */
uint64_t xnu_boot_get_entry(const xnu_boot_state_t *state);

/* Get the kernel virtual entry point (for vCPU PC when MMU on) */
uint64_t xnu_boot_get_virt_entry(const xnu_boot_state_t *state);

/* Get the kernel virtual base address (linked address) */
uint64_t xnu_boot_get_virt_base(const xnu_boot_state_t *state);

/* Get the BootArgs address (for setting vCPU x0) */
uint64_t xnu_boot_get_bootargs_addr(const xnu_boot_state_t *state);

/* Clean up */
void xnu_boot_cleanup(xnu_boot_state_t *state);

/* Print boot configuration summary */
void xnu_boot_print_summary(const xnu_boot_state_t *state);

#endif /* SILICONV_XNU_BOOT_H */
