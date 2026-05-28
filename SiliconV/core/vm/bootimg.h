/*
 * SiliconV — Android Boot Image Parser
 *
 * Parses Android boot.img (v0, v1, v2, v3, v4) format.
 * Extracts kernel, ramdisk, DTB, and command line.
 *
 * Reference: Android Boot Image Header Format
 * https://source.android.com/docs/core/architecture/bootimg
 */

#ifndef SILICONV_BOOTIMG_H
#define SILICONV_BOOTIMG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Magic ─────────────────────────────────────── */
#define BOOT_MAGIC          "ANDROID!"
#define BOOT_MAGIC_SIZE     8
#define BOOT_NAME_SIZE      16
#define BOOT_ARGS_SIZE      512
#define BOOT_EXTRA_ARGS_SIZE 1024

/* ── Boot Image Header (v0) ────────────────────── */
typedef struct {
    uint8_t  magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t kernel_addr;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;
    uint32_t second_size;
    uint32_t second_addr;
    uint32_t tags_addr;
    uint32_t page_size;
    uint32_t header_version;  /* 0 for v0 */
    uint32_t os_version;
    uint8_t  name[BOOT_NAME_SIZE];
    uint8_t  cmdline[BOOT_ARGS_SIZE];
    uint32_t id[4];
    uint8_t  extra_cmdline[BOOT_EXTRA_ARGS_SIZE];
} __attribute__((packed)) boot_img_hdr_v0;

/* ── Boot Image Header (v1) ────────────────────── */
typedef struct {
    boot_img_hdr_v0 v0;
    uint32_t recovery_dtbo_size;
    uint32_t recovery_dtbo_offset;
    uint32_t header_size;
} __attribute__((packed)) boot_img_hdr_v1;

/* ── Boot Image Header (v2) ────────────────────── */
typedef struct {
    boot_img_hdr_v1 v1;
    uint32_t dtb_size;
    uint64_t dtb_addr;
} __attribute__((packed)) boot_img_hdr_v2;

/* ── Boot Image Header (v3) ────────────────────── */
typedef struct {
    uint8_t  magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t ramdisk_size;
    uint32_t os_version;
    uint32_t header_size;
    uint32_t reserved[4];
    uint32_t header_version;
    uint32_t cmdline[BOOT_ARGS_SIZE / 4];
    uint32_t signature[4];  /* Boot signature */
} __attribute__((packed)) boot_img_hdr_v3;

/* ── Boot Image Header (v4) ────────────────────── */
typedef struct {
    boot_img_hdr_v3 v3;
    uint32_t boot_img_size;
    uint32_t recovery_dtbo_size;
    uint32_t recovery_dtbo_offset;
    uint32_t dtb_size;
    uint64_t dtb_addr;
} __attribute__((packed)) boot_img_hdr_v4;

/* ── Parsed Boot Image ─────────────────────────── */
typedef struct {
    int      version;          /* 0-4 */
    uint32_t page_size;

    /* Kernel */
    uint8_t *kernel;
    uint32_t kernel_size;
    uint32_t kernel_addr;

    /* Ramdisk */
    uint8_t *ramdisk;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;

    /* DTB */
    uint8_t *dtb;
    uint32_t dtb_size;
    uint64_t dtb_addr;

    /* Command line */
    char     cmdline[BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE + 1];

    /* Second stage bootloader */
    uint8_t *second;
    uint32_t second_size;

    /* Recovery DTBO */
    uint8_t *recovery_dtbo;
    uint32_t recovery_dtbo_size;

    /* Raw image (caller owns this) */
    uint8_t *raw;
    size_t   raw_size;
} sv_bootimg_t;

/* ── API ───────────────────────────────────────── */

/* Parse a boot.img from memory buffer.
 * Returns 0 on success, -1 on error.
 * The parsed image points into the raw buffer (no copies).
 */
int sv_bootimg_parse(sv_bootimg_t *img, uint8_t *data, size_t size);

/* Load a boot.img from file.
 * Caller must free img->raw when done.
 */
int sv_bootimg_load(sv_bootimg_t *img, const char *path);

/* Free a loaded boot image */
void sv_bootimg_free(sv_bootimg_t *img);

/* Print boot image info */
void sv_bootimg_dump(const sv_bootimg_t *img);

/* Get the kernel load address for direct boot */
uint64_t sv_bootimg_kernel_addr(const sv_bootimg_t *img);

/* Get DTB address (or 0 if no DTB) */
uint64_t sv_bootimg_dtb_addr(const sv_bootimg_t *img);

#endif /* SILICONV_BOOTIMG_H */
