/*
 * SiliconV — Apple XNU BootArgs Structure
 *
 * Defines the BootArgs structures that XNU expects at boot time.
 * These are placed in guest RAM and pointed to by x0 at kernel entry.
 *
 * Two revisions:
 *   - AppleKernelBootArgsRev2 (used by iOS < 17 / macOS < 14)
 *   - AppleKernelBootArgsRev3 (used by iOS 17+ / macOS 14+)
 */

#ifndef SILICONV_BOOTARGS_H
#define SILICONV_BOOTARGS_H

#include <stdint.h>

/* ── BootArgs Rev2 (legacy, most common) ────────── */
#define BOOTARGS_REV2_MAGIC      0x424F4F54  /* "BOOT" */
#define BOOTARGS_REV2_VERSION    2

typedef struct __attribute__((packed)) {
    uint16_t    Revision;            /* Revision = 2 */
    uint16_t    Version;             /* Version */
    uint32_t    Magic;               /* Magic = "BOOT" */

    /* Video information */
    uint32_t    video_baseAddr_lo;
    uint32_t    video_baseAddr_hi;
    uint32_t    video_display;
    uint32_t    video_rowBytes;
    uint32_t    video_width;
    uint32_t    video_height;
    uint32_t    video_depth;

    /* DeviceTree */
    uint32_t    deviceTree_lo;       /* DTB physical address (low) */
    uint32_t    deviceTree_hi;       /* DTB physical address (high) */

    /* Command line */
    uint32_t    cmdLine_len;
    uint32_t    cmdLine_off;         /* Offset from BootArgs base */

    /* Memory map */
    uint32_t    memDescriptor_off;
    uint32_t    memDescriptor_count;
    uint32_t    memMap_off;

    /* Boot flags */
    uint32_t    flags;               /* Boot flags */

    /* Extra */
    uint32_t    boot_flags_high;
    uint32_t    resv[15];            /* Padding */

    /* NVRAM proxy */
    uint32_t    nvram_off;           /* Offset to NVRAM data */
    uint32_t    nvram_size;

    /* Random seeds */
    uint32_t    seed_off;
    uint32_t    seed_size;
    uint8_t     seed_data[32];

    /* Boot arguments string follows */
} AppleKernelBootArgsRev2;

/* ── BootArgs Rev3 (iOS 17+ / macOS 14+) ───────── */
#define BOOTARGS_REV3_MAGIC      0x42585246  /* "BXR\0"? Actually "FXRB" */

typedef struct __attribute__((packed)) {
    uint64_t    RevisionVersion;     /* Revision and version packed */
    uint32_t    Magic;               /* Magic */

    uint32_t    flags;
    uint32_t    flags_high;
    uint32_t    boot_ram_size;

    uint64_t    device_tree_addr;    /* DTB physical address */

    uint64_t    nvram_data_addr;
    uint64_t    nvram_data_size;

    uint64_t    seed_addr;
    uint64_t    seed_size;

    uint64_t    video_baseAddr;
    uint32_t    video_width;
    uint32_t    video_height;
    uint32_t    video_depth;
    uint32_t    video_rowBytes;

    uint64_t    cmdLine_addr;
    uint64_t    cmdLine_size;

    uint64_t    memmap_phys_addr;
    uint64_t    memmap_entry_count;

    uint8_t     padding[0x100 - 0x80]; /* Fill to 256 bytes */
} AppleKernelBootArgsRev3;

/* ── Boot flags ────────────────────────────────── */
#define BOOTARGS_FLAG_NORMAL        0x00000000
#define BOOTARGS_FLAG_RESTORE       0x00000001
#define BOOTARGS_FLAG_SAFE_MODE     0x00000002
#define BOOTARGS_FLAG_DARK_BOOT     0x00010000
#define BOOTARGS_FLAG_VERBOSE       0x00020000

/* ── API ───────────────────────────────────────── */

/* Setup BootArgs Rev2 at the given address in guest RAM.
 * bootargs_pa specifies the physical address where BootArgs is placed.
 * Returns the size written, or -1 on error. */
int bootargs_setup_rev2(uint8_t *guest_ram, uint64_t ram_base,
                         uint64_t ram_size,
                         uint64_t bootargs_pa,
                         uint64_t dtb_addr,
                         const char *cmdline,
                         uint32_t flags);

/* Setup BootArgs Rev3 at the given address in guest RAM.
 * bootargs_pa specifies the physical address where BootArgs is placed.
 * Returns the size written, or -1 on error. */
int bootargs_setup_rev3(uint8_t *guest_ram, uint64_t ram_base,
                         uint64_t ram_size,
                         uint64_t bootargs_pa,
                         uint64_t dtb_addr,
                         const char *cmdline,
                         uint32_t flags);

/* Determine which BootArgs revision to use based on kernel version.
 * iOS 17+ / macOS 14+ → Rev3, else Rev2 */
int bootargs_select_revision(const char *kernel_version);

#endif /* SILICONV_BOOTARGS_H */
