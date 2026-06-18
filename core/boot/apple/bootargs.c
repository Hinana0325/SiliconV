/*
 * SiliconV — Apple XNU BootArgs Setup (Implementation)
 *
 * Writes BootArgs structures into guest RAM for XNU kernel boot.
 * Supports both Rev2 (legacy) and Rev3 (iOS 17+ / macOS 14+).
 */

#include "bootargs.h"
#include <string.h>
#include <stdio.h>

int bootargs_setup_rev2(uint8_t *guest_ram, uint64_t ram_base,
                         uint64_t ram_size,
                         uint64_t bootargs_buf_offset,
                         uint64_t dtb_addr,
                         const char *cmdline,
                         uint32_t flags)
{
    uint64_t remaining = ram_size - bootargs_buf_offset;

    if (remaining < sizeof(AppleKernelBootArgsRev2) + 512)
        return -1;

    AppleKernelBootArgsRev2 *args =
        (AppleKernelBootArgsRev2 *)(guest_ram + bootargs_buf_offset);

    memset(args, 0, sizeof(*args));

    args->Revision = 2;
    args->Version = 24;  /* iOS 16 compatible */
    args->Magic = BOOTARGS_REV2_MAGIC;

    /* Video info */
    args->video_baseAddr_lo = 0;
    args->video_baseAddr_hi = 0;
    args->video_display = 0;
    args->video_rowBytes = 0;
    args->video_width = 0;
    args->video_height = 0;
    args->video_depth = 0;

    /* DTB address */
    args->deviceTree_lo = (uint32_t)(dtb_addr & 0xFFFFFFFF);
    args->deviceTree_hi = (uint32_t)((dtb_addr >> 32) & 0xFFFFFFFF);

    /* Command line */
    if (cmdline) {
        uint32_t cmdline_len = (uint32_t)(strlen(cmdline) + 1);
        uint32_t cmdline_off = sizeof(AppleKernelBootArgsRev2);

        if (cmdline_off + cmdline_len > remaining)
            cmdline_len = (uint32_t)(remaining - cmdline_off);

        memcpy((uint8_t *)args + cmdline_off, cmdline, cmdline_len);
        args->cmdLine_len = cmdline_len;
        args->cmdLine_off = cmdline_off;
    }

    /* Boot flags */
    args->flags = flags;

    /* NVRAM pointer (none) */
    args->nvram_off = 0;
    args->nvram_size = 0;

    /* Random seed */
    args->seed_off = 0;
    args->seed_size = 0;

    return (int)sizeof(AppleKernelBootArgsRev2);
}

int bootargs_setup_rev3(uint8_t *guest_ram, uint64_t ram_base,
                         uint64_t ram_size,
                         uint64_t bootargs_buf_offset,
                         uint64_t dtb_addr,
                         const char *cmdline,
                         uint32_t flags)
{
    uint64_t remaining = ram_size - bootargs_buf_offset;

    if (remaining < sizeof(AppleKernelBootArgsRev3) + 512)
        return -1;

    AppleKernelBootArgsRev3 *args =
        (AppleKernelBootArgsRev3 *)(guest_ram + bootargs_buf_offset);

    memset(args, 0, sizeof(*args));

    args->RevisionVersion = (3ULL << 32) | 1; /* Rev 3, Version 1 */
    args->Magic = BOOTARGS_REV2_MAGIC; /* May differ on real hardware */

    args->flags = flags;
    args->flags_high = 0;
    args->boot_ram_size = (uint32_t)(ram_size >> 20); /* MB */

    args->device_tree_addr = dtb_addr;

    args->nvram_data_addr = 0;
    args->nvram_data_size = 0;

    args->seed_addr = 0;
    args->seed_size = 0;

    args->video_baseAddr = 0;
    args->video_width = 0;
    args->video_height = 0;
    args->video_depth = 0;
    args->video_rowBytes = 0;

    /* Command line — place after the structure */
    if (cmdline) {
        uint32_t cmdline_len = (uint32_t)(strlen(cmdline) + 1);
        uint64_t cmdline_buf_offset = bootargs_buf_offset + sizeof(AppleKernelBootArgsRev3);
        uint64_t cmdline_guest_pa = ram_base + cmdline_buf_offset; /* guest physical address */

        if (cmdline_buf_offset + cmdline_len < ram_size) {
            memcpy(guest_ram + cmdline_buf_offset, cmdline, cmdline_len);
            args->cmdLine_addr = cmdline_guest_pa;
            args->cmdLine_size = cmdline_len;
        }
    }

    return (int)sizeof(AppleKernelBootArgsRev3);
}

int bootargs_select_revision(const char *kernel_version)
{
    /* Simple heuristic: if version string contains "22" (iOS 17.x),
     * use Rev3. Otherwise default to Rev2. */
    if (kernel_version) {
        /* XNU version strings look like "xnu-8792.61.2~1" */
        /* iOS 17 = xnu-8792+, macOS 14 = xnu-8792+ */
        if (strstr(kernel_version, "xnu-8") ||
            strstr(kernel_version, "xnu-9") ||
            strstr(kernel_version, "xnu-10"))
            return 3;
    }
    return 2;
}
