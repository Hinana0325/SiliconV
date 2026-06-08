/*
 * SiliconV — Android Boot Image Parser (Implementation)
 *
 * Supports boot image versions 0 through 4.
 * Handles page-aligned segments correctly.
 */

#include "bootimg.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Align offset to page boundary */
static uint32_t align_up(uint32_t val, uint32_t page_size)
{
    return (val + page_size - 1) & ~(page_size - 1);
}

/* Get header size for a given version */
static uint32_t header_size_for_version(int version)
{
    switch (version) {
    case 0: return sizeof(boot_img_hdr_v0);
    case 1: return sizeof(boot_img_hdr_v1);
    case 2: return sizeof(boot_img_hdr_v2);
    case 3: return sizeof(boot_img_hdr_v3);
    case 4: return sizeof(boot_img_hdr_v4);
    default: return 0;
    }
}

int sv_bootimg_parse(sv_bootimg_t *img, uint8_t *data, size_t size)
{
    if (!img || !data || size < 256)
        return -1;

    memset(img, 0, sizeof(*img));
    img->raw = data;
    img->raw_size = size;

    /* Check magic */
    if (memcmp(data, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0) {
        fprintf(stderr, "bootimg: invalid magic\n");
        return -1;
    }

    /* Determine version from header_version field */
    /* In v0/v1/v2: header_version is at offset 40 (in v0 header) */
    /* In v3/v4: header_version is at offset 28 */
    uint32_t ver = *(uint32_t*)(data + 40);  /* Try v0 location first */
    if (ver > 4) {
        ver = *(uint32_t*)(data + 28);  /* Try v3 location */
    }

    img->version = ver;

    if (ver <= 2) {
        /* v0/v1/v2 share the same base header */
        boot_img_hdr_v0 *hdr = (boot_img_hdr_v0*)data;
        img->page_size = hdr->page_size;
        img->kernel_size = hdr->kernel_size;
        img->kernel_addr = hdr->kernel_addr;
        img->ramdisk_size = hdr->ramdisk_size;
        img->ramdisk_addr = hdr->ramdisk_addr;
        img->second_size = hdr->second_size;

        /* Command line */
        memcpy(img->cmdline, hdr->cmdline, BOOT_ARGS_SIZE);
        if (ver >= 1) {
            boot_img_hdr_v1 *hdr1 = (boot_img_hdr_v1*)data;
            strncat(img->cmdline, (char*)hdr1->v0.extra_cmdline,
                    BOOT_EXTRA_ARGS_SIZE);
        }

        /* Page-aligned offsets */
        uint32_t header_sz = (ver >= 1) ?
            ((boot_img_hdr_v1*)data)->header_size :
            align_up(sizeof(boot_img_hdr_v0), img->page_size);

        uint32_t offset = header_sz;

        /* Kernel */
        img->kernel = data + offset;
        offset += align_up(img->kernel_size, img->page_size);

        /* Ramdisk */
        img->ramdisk = data + offset;
        offset += align_up(img->ramdisk_size, img->page_size);

        /* Second stage */
        if (img->second_size > 0) {
            img->second = data + offset;
            offset += align_up(img->second_size, img->page_size);
        }

        /* Recovery DTBO (v1+) */
        if (ver >= 1) {
            boot_img_hdr_v1 *hdr1 = (boot_img_hdr_v1*)data;
            img->recovery_dtbo_size = hdr1->recovery_dtbo_size;
            if (img->recovery_dtbo_size > 0) {
                /* After second, or after ramdisk if no second */
                offset += align_up(img->second_size, img->page_size);
                img->recovery_dtbo = data + hdr1->recovery_dtbo_offset;
            }
        }

        /* DTB (v2+) */
        if (ver >= 2) {
            boot_img_hdr_v2 *hdr2 = (boot_img_hdr_v2*)data;
            img->dtb_size = hdr2->dtb_size;
            img->dtb_addr = hdr2->dtb_addr;
            if (img->dtb_size > 0) {
                /* DTB follows recovery DTBO */
                img->dtb = data + offset;
            }
        }
    } else {
        /* v3/v4 */
        boot_img_hdr_v3 *hdr3 = (boot_img_hdr_v3*)data;
        img->kernel_size = hdr3->kernel_size;
        img->ramdisk_size = hdr3->ramdisk_size;
        img->page_size = 4096;  /* v3+ always 4K pages */

        /* Command line (v3 uses uint32_t array) */
        memcpy(img->cmdline, hdr3->cmdline, BOOT_ARGS_SIZE);

        uint32_t offset = hdr3->header_size;

        /* Kernel */
        img->kernel = data + offset;
        offset += align_up(img->kernel_size, img->page_size);

        /* Ramdisk */
        img->ramdisk = data + offset;
        offset += align_up(img->ramdisk_size, img->page_size);

        /* DTB (v4) */
        if (ver >= 4) {
            boot_img_hdr_v4 *hdr4 = (boot_img_hdr_v4*)data;
            img->dtb_size = hdr4->dtb_size;
            img->dtb_addr = hdr4->dtb_addr;
            if (img->dtb_size > 0) {
                img->dtb = data + offset;
            }
        }
    }

    return 0;
}

int sv_bootimg_load(sv_bootimg_t *img, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bootimg: cannot open %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc(size);
    if (!data) {
        fclose(f);
        return -1;
    }

    size_t nread = fread(data, 1, size, f);
    fclose(f);

    if ((long)nread != size) {
        free(data);
        return -1;
    }

    int ret = sv_bootimg_parse(img, data, size);
    if (ret != 0) {
        free(data);
        return -1;
    }

    return 0;
}

void sv_bootimg_free(sv_bootimg_t *img)
{
    if (img && img->raw) {
        free(img->raw);
        img->raw = NULL;
    }
}

void sv_bootimg_dump(const sv_bootimg_t *img)
{
    if (!img) return;

    printf("Android Boot Image v%d\n", img->version);
    printf("  Page size:    %u\n", img->page_size);
    printf("  Kernel:       %u bytes @ 0x%08x\n",
           img->kernel_size, img->kernel_addr);
    printf("  Ramdisk:      %u bytes @ 0x%08x\n",
           img->ramdisk_size, img->ramdisk_addr);
    if (img->dtb_size > 0)
        printf("  DTB:          %u bytes @ 0x%08llx\n",
               img->dtb_size, (unsigned long long)img->dtb_addr);
    printf("  Cmdline:      %s\n", img->cmdline);
}

uint64_t sv_bootimg_kernel_addr(const sv_bootimg_t *img)
{
    if (!img) return 0;
    /* For v3/v4, kernel_addr is not in header; use default */
    if (img->kernel_addr != 0)
        return img->kernel_addr;
    return 0x40080000;  /* Default ARM64 kernel load address */
}

uint64_t sv_bootimg_dtb_addr(const sv_bootimg_t *img)
{
    if (!img || img->dtb_size == 0) return 0;
    if (img->dtb_addr != 0)
        return img->dtb_addr;
    /* Place DTB after kernel + 2MB */
    return sv_bootimg_kernel_addr(img) + 0x200000;
}
