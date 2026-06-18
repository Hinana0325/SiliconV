/*
 * SiliconV — XNU Boot Loader (Implementation)
 *
 * Ties together IMG4 parsing, Mach-O loading, DeviceTree handling,
 * and BootArgs setup to boot Apple XNU kernels.
 *
 * This is the Apple equivalent of the Android boot.img loader.
 */

#include "xnu_boot.h"
#include "img4.h"
#include "macho.h"
#include "dtre.h"
#include "bootargs.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void xnu_boot_init(xnu_boot_state_t *state, const xnu_boot_config_t *config)
{
    memset(state, 0, sizeof(*state));
    if (config)
        state->config = *config;
}

int xnu_boot_load_kernel(xnu_boot_state_t *state, const uint8_t *data, size_t size)
{
    if (!data || size == 0) return -1;

    state->kernel_data = (uint8_t *)data;
    state->kernel_size = size;

    /* Try parsing as IMG4 container first */
    img4_container_t img4;
    if (img4_load(&img4, data, size) == 0) {
        if (img4.payload.type == IMG4_PAYLOAD_KRNL ||
            img4.payload.type == IMG4_PAYLOAD_RAW) {
            /* Use the payload */
            state->kernel_data = img4.payload.data;
            state->kernel_size = img4.payload.data_size;
            printf("xnu_boot: kernel from IMG4 payload (%s, %u bytes)\n",
                   img4_payload_type_name(img4.payload.type),
                   img4.payload.data_size);
        }

        /* If this is a multi-payload IMG4, check for DTB and ramdisk */
        /* (handled in xnu_boot_setup) */
    } else {
        printf("xnu_boot: kernel loaded as raw binary (%zu bytes)\n", size);
    }

    /* Validate Mach-O header */
    macho_context_t macho;
    if (macho_parse(&macho, state->kernel_data, state->kernel_size) < 0) {
        fprintf(stderr, "xnu_boot: kernel is not a valid Mach-O\n");
        return -1;
    }

    state->boot_info.is_fileset = macho.is_fileset;
    macho_free(&macho);

    printf("xnu_boot: kernel validated (%s)\n",
           state->boot_info.is_fileset ? "MH_FILESET" : "MH_EXECUTE");

    return 0;
}

int xnu_boot_load_dtb(xnu_boot_state_t *state, const uint8_t *data, size_t size)
{
    if (!data || size == 0) return -1;

    /* Try parsing as IMG4 container first (DER-wrapped DeviceTree) */
    img4_container_t img4;
    if (img4_load(&img4, data, size) == 0) {
        if (img4.payload.type == IMG4_PAYLOAD_DTRE ||
            img4.payload.type == IMG4_PAYLOAD_RAW) {
            /* Copy decompressed data so it outlives the img4 container */
            state->dtb_data = (uint8_t *)malloc(img4.payload.data_size);
            if (state->dtb_data) {
                memcpy(state->dtb_data, img4.payload.data, img4.payload.data_size);
                state->dtb_size = img4.payload.data_size;
                printf("xnu_boot: DTB from IMG4 payload (%s, %u bytes)%s\n",
                       img4_payload_type_name(img4.payload.type),
                       img4.payload.data_size,
                       img4.payload.is_compressed ? " (decompressed)" : "");
            } else {
                state->dtb_data = (uint8_t *)data;
                state->dtb_size = size;
            }
        } else {
            /* Wrong payload type — fall through to raw */
            state->dtb_data = (uint8_t *)data;
            state->dtb_size = size;
        }
        img4_free(&img4);
    } else {
        state->dtb_data = (uint8_t *)data;
        state->dtb_size = size;
        printf("xnu_boot: DTB loaded as raw (%zu bytes)\n", size);
    }

    return 0;
}

int xnu_boot_setup(xnu_boot_state_t *state,
                    uint8_t *guest_ram, uint64_t ram_base, uint64_t ram_size)
{
    if (!state->kernel_data) {
        fprintf(stderr, "xnu_boot: no kernel loaded\n");
        return -1;
    }

    /* ── 1. Parse kernel Mach-O ──────────────────── */
    macho_context_t macho;
    if (macho_parse(&macho, state->kernel_data, state->kernel_size) < 0) {
        return -1;
    }

    /* ── 2. Determine ASLR slide ─────────────────── */
    uint64_t aslr = state->config.aslr_slide;
    if (aslr == 0) {
        /* Auto: use a default slide based on RAM layout */
        aslr = 0x1000; /* Small default slide for emulation */
    } else if (aslr == ~0ULL) {
        aslr = 0; /* Disabled */
    }
    state->boot_info.aslr_slide = aslr;

    /* ── 3. Load kernel segments into guest RAM ──── */
    uint64_t entry = macho_load(&macho, guest_ram, ram_base, ram_size, aslr);
    if (entry == 0) {
        fprintf(stderr, "xnu_boot: failed to load kernel\n");
        macho_free(&macho);
        return -1;
    }
    state->boot_info.entry_point = entry;

    /* Store virtual entry and virtual base */
    state->boot_info.virt_base = macho.base_addr;
    if (macho.has_entry) {
        /* The virtual entry is the original link-time entry point (before ASLR) */
        state->boot_info.entry_point_virt = macho.entry_point;
        printf("xnu_boot: kernel entry (virt) = 0x%lx, base (virt) = 0x%lx\n",
               (unsigned long)macho.entry_point,
               (unsigned long)macho.base_addr);
    } else {
        state->boot_info.entry_point_virt = macho.base_addr;
    }

    /* ── 4. Setup DeviceTree ─────────────────────── */
    apple_dt_node_t *dt_root = NULL;

    if (state->dtb_data) {
        /* Load DTB from provided data */
        dt_root = apple_dt_load(state->dtb_data, state->dtb_size);
    }

    if (!dt_root) {
        /* Create minimal DeviceTree from scratch */
        dt_root = apple_dt_node_new("/");
        if (dt_root) {
            apple_dt_set_prop_str(dt_root, "compatible", "siliconv,apple-vm-v0");
            apple_dt_set_prop_str(dt_root, "model", "SiliconV Apple Virtual Machine");

            /* Add /chosen node */
            apple_dt_node_t *chosen = apple_dt_node_new("chosen");
            apple_dt_add_child(dt_root, chosen);
        }
    }

    if (dt_root) {
        /* Populate runtime data */
        apple_dt_populate_runtime(dt_root,
                                   ram_base, ram_size,
                                   state->config.cmdline);

        /* Print stats for debugging */
        apple_dt_print_stats(dt_root);

        /* Filter unimplemented devices */
        apple_dt_filter_nodes(dt_root);
        apple_dt_print_stats(dt_root);

        /* Serialize to guest RAM */
        uint8_t dtb_buf[65536]; /* 64KB max */
        int dtb_size = apple_dt_serialize(dt_root, dtb_buf, sizeof(dtb_buf));

        if (dtb_size > 0) {
            /* Place DTB at a fixed offset in guest RAM */
            uint64_t dtb_buf_offset = ram_size - 0x100000; /* 1MB from top of buffer */
            if (dtb_buf_offset + dtb_size < ram_size) {
                memcpy(guest_ram + dtb_buf_offset, dtb_buf, dtb_size);
                /* Store guest physical address (ram_base + buffer offset) */
                state->boot_info.dtb_addr = ram_base + dtb_buf_offset;
                printf("xnu_boot: DTB at guest phys 0x%lx (buf offset 0x%lx, %d bytes)\n",
                       (unsigned long)state->boot_info.dtb_addr,
                       (unsigned long)dtb_buf_offset, dtb_size);
            }
        }

        apple_dt_free(dt_root);
    }

    /* ── 5. Setup BootArgs ───────────────────────── */
    uint32_t boot_flags = state->config.boot_flags;
    if (boot_flags == 0)
        boot_flags = 0x00020000; /* VERBOSE */

    int rev = state->config.bootargs_revision;
    if (rev == 0) {
        /* Auto-detect based on RAM size heuristic */
        rev = (ram_size >= 0x200000000ULL) ? 3 : 2;
    }

    /* Place BootArgs right below DTB (DTB at top-1MB, BootArgs at top-8KB) */
    uint64_t bootargs_buf_offset = ram_size - 0x2000; /* 8KB from top of buffer */
    uint64_t bootargs_pa = ram_base + bootargs_buf_offset; /* guest physical address */
    int bootargs_size;

    if (rev == 3) {
        bootargs_size = bootargs_setup_rev3(guest_ram, ram_base, ram_size,
                                             bootargs_buf_offset,
                                             state->boot_info.dtb_addr,
                                             state->config.cmdline,
                                             boot_flags);
    } else {
        bootargs_size = bootargs_setup_rev2(guest_ram, ram_base, ram_size,
                                             bootargs_buf_offset,
                                             state->boot_info.dtb_addr,
                                             state->config.cmdline,
                                             boot_flags);
    }

    if (bootargs_size > 0) {
        state->boot_info.bootargs_addr = bootargs_pa;
        printf("xnu_boot: BootArgs Rev%d at 0x%lx (%d bytes)\n",
               rev, (unsigned long)bootargs_pa, bootargs_size);
    }

    macho_free(&macho);
    return 0;
}

uint64_t xnu_boot_get_entry(const xnu_boot_state_t *state)
{
    return state->boot_info.entry_point;
}

uint64_t xnu_boot_get_virt_entry(const xnu_boot_state_t *state)
{
    return state->boot_info.entry_point_virt;
}

uint64_t xnu_boot_get_virt_base(const xnu_boot_state_t *state)
{
    return state->boot_info.virt_base;
}

uint64_t xnu_boot_get_bootargs_addr(const xnu_boot_state_t *state)
{
    return state->boot_info.bootargs_addr;
}

void xnu_boot_cleanup(xnu_boot_state_t *state)
{
    /* state doesn't own the data buffers */
    memset(state, 0, sizeof(*state));
}

void xnu_boot_print_summary(const xnu_boot_state_t *state)
{
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  SiliconV — XNU Boot Summary\n");
    printf("  Kernel PA:   0x%lx\n",
           (unsigned long)state->boot_info.entry_point);
    printf("  Kernel VA:   0x%lx\n",
           (unsigned long)state->boot_info.entry_point_virt);
    printf("  Kernel Base: 0x%lx (%s)\n",
           (unsigned long)state->boot_info.virt_base,
           state->boot_info.is_fileset ? "MH_FILESET" : "MH_EXECUTE");
    printf("  DTB:      0x%lx\n",
           (unsigned long)state->boot_info.dtb_addr);
    printf("  BootArgs: 0x%lx\n",
           (unsigned long)state->boot_info.bootargs_addr);
    printf("  ASLR:     0x%lx\n",
           (unsigned long)state->boot_info.aslr_slide);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\n");
}
