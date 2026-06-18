/*
 * SiliconV — XNU Boot Component Unit Tests
 *
 * Tests the IMG4 parser and BootArgs setup without requiring
 * a real XNU kernel.
 *
 * These tests verify:
 *   - IMG4 container detection
 *   - Raw binary fallback
 *   - BootArgs Rev2 structure generation
 *   - BootArgs Rev3 structure generation
 */

#include "../../core/boot/apple/img4.h"
#include "../../core/boot/apple/bootargs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* ── Test IMG4 raw binary fallback ──────────────── */
static void test_img4_raw(void)
{
    printf("test_img4_raw...\n");

    uint8_t data[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

    img4_container_t img4;
    int ret = img4_load(&img4, data, sizeof(data));

    assert(ret == 0);
    assert(img4.valid == true);
    assert(img4.payload.type == IMG4_PAYLOAD_RAW);
    assert(img4.payload.data_size == sizeof(data));
    assert(img4.payload.data == data);

    img4_free(&img4);
    printf("  PASS\n");
}

/* ── Test IMG4 container detection ──────────────── */
static void test_img4_detect(void)
{
    printf("test_img4_detect...\n");

    /* Not a container */
    uint8_t not_container[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };
    assert(img4_is_container(not_container, 8) == false);

    /* Too small */
    uint8_t too_small[] = { 0x00, 0x00, 0x00, 0x00 };
    assert(img4_is_container(too_small, 4) == false);

    /* Raw binary should be handled by img4_load regardless */
    uint8_t raw[] = { 0x11, 0x22, 0x33, 0x44 };
    img4_container_t img4;
    assert(img4_load(&img4, raw, sizeof(raw)) == 0);
    assert(img4.payload.type == IMG4_PAYLOAD_RAW);

    printf("  PASS\n");
}

/* ── Test IMG4 payload type detection ──────────── */
static void test_img4_types(void)
{
    printf("test_img4_types...\n");

    assert(img4_payload_type_from_str("krnl") == IMG4_PAYLOAD_KRNL);
    assert(img4_payload_type_from_str("dtre") == IMG4_PAYLOAD_DTRE);
    assert(img4_payload_type_from_str("rdsk") == IMG4_PAYLOAD_RDSK);
    assert(img4_payload_type_from_str("trst") == IMG4_PAYLOAD_TRST);
    assert(img4_payload_type_from_str("rtsc") == IMG4_PAYLOAD_TRST);
    assert(img4_payload_type_from_str("sepf") == IMG4_PAYLOAD_SEPF);
    assert(img4_payload_type_from_str("xxxx") == IMG4_PAYLOAD_UNKNOWN);

    assert(strcmp(img4_payload_type_name(IMG4_PAYLOAD_KRNL), "kernel") == 0);
    assert(strcmp(img4_payload_type_name(IMG4_PAYLOAD_UNKNOWN), "unknown") == 0);

    printf("  PASS\n");
}

/* ── Test BootArgs Rev2 ─────────────────────────── */
static void test_bootargs_rev2(void)
{
    printf("test_bootargs_rev2...\n");

    /* Allocate simulated guest RAM */
    uint64_t ram_size = 0x10000000; /* 256 MB */
    uint64_t ram_base = 0x800000000ULL;
    uint8_t *guest_ram = calloc(1, ram_size);
    assert(guest_ram != NULL);

    uint64_t dtb_addr = ram_size - 0x100000; /* 1MB from top */
    uint64_t bootargs_pa = ram_size - 0x2000; /* 8KB from top */
    const char *cmdline = "-v debug=0x8 keepsyms=1";

    int size = bootargs_setup_rev2(guest_ram, ram_base, ram_size,
                                    bootargs_pa, dtb_addr,
                                    cmdline, 0x00020000);

    assert(size > 0);

    AppleKernelBootArgsRev2 *args =
        (AppleKernelBootArgsRev2 *)(guest_ram + bootargs_pa);

    assert(args->Revision == 2);
    assert(args->Magic == BOOTARGS_REV2_MAGIC);
    assert(args->flags == 0x00020000);

    /* Check DTB address */
    assert(args->deviceTree_lo == (uint32_t)(dtb_addr & 0xFFFFFFFF));

    /* Check cmdline was copied */
    assert(args->cmdLine_len > 0);
    const char *copied_cmdline = (const char *)args + args->cmdLine_off;
    assert(strcmp(copied_cmdline, cmdline) == 0);

    free(guest_ram);
    printf("  PASS\n");
}

/* ── Test BootArgs Rev3 ─────────────────────────── */
static void test_bootargs_rev3(void)
{
    printf("test_bootargs_rev3...\n");

    uint64_t ram_size = 0x40000000; /* 1 GB */
    uint64_t ram_base = 0x800000000ULL;
    uint8_t *guest_ram = calloc(1, ram_size);
    assert(guest_ram != NULL);

    uint64_t dtb_addr = ram_size - 0x100000;
    uint64_t bootargs_pa = ram_size - 0x2000;
    const char *cmdline = "-v debug=0x8";

    int size = bootargs_setup_rev3(guest_ram, ram_base, ram_size,
                                    bootargs_pa, dtb_addr,
                                    cmdline, 0x00020000);

    assert(size > 0);

    AppleKernelBootArgsRev3 *args =
        (AppleKernelBootArgsRev3 *)(guest_ram + bootargs_pa);

    assert(args->RevisionVersion >> 32 == 3);
    assert(args->device_tree_addr == dtb_addr);
    assert(args->flags == 0x00020000);

    free(guest_ram);
    printf("  PASS\n");
}

int main(void)
{
    printf("\n=== XNU Boot Component Tests ===\n\n");

    test_img4_raw();
    test_img4_detect();
    test_img4_types();
    test_bootargs_rev2();
    test_bootargs_rev3();

    printf("\n=== All XNU boot tests passed! ===\n");
    return 0;
}
