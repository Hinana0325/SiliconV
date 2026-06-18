/*
 * SiliconV — Mach-O Loader for XNU Kernel
 *
 * Parses Mach-O binaries (MH_EXECUTE and MH_FILESET) used
 * by Apple XNU kernels. Loads segments into guest RAM.
 *
 * Supports:
 *   - MH_MAGIC_64 / MH_CIGAM_64 (64-bit Mach-O)
 *   - LC_SEGMENT_64 for segment loading
 *   - LC_UNIXTHREAD for entry point
 *   - MH_FILESET for modern iOS kernel caches
 */

#ifndef SILICONV_MACHO_H
#define SILICONV_MACHO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Mach-O Constants ──────────────────────────── */
#define MH_MAGIC_64     0xFEEDFACF
#define MH_CIGAM_64     0xCFFAEDFE
#define MH_EXECUTE      0x2
#define MH_FILESET      0xC
#define MH_OBJECT       0x1

/* Load commands */
#define LC_SEGMENT_64           0x19
#define LC_UNIXTHREAD           0x5
#define LC_SYMTAB               0x2
#define LC_FILESET_ENTRY        0x4B
#define LC_FILESET_UNIXTHREAD   0x4C

/* ARM64 thread state flavor */
#define ARM_THREAD_STATE64      6
#define ARM_THREAD_STATE64_COUNT 68  /* x0-x28 + fp + lr + sp + pc + cpsr = 33 regs */

/* ── Headers ───────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} mach_header_64_t;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} segment_command_64_t;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t flavor;
    uint32_t count;
    /* state follows */
} thread_command_t;

typedef struct __attribute__((packed)) {
    uint64_t x[29];   /* x0-x28 */
    uint64_t fp;       /* x29 */
    uint64_t lr;       /* x30 */
    uint64_t sp;       /* x31 */
    uint64_t pc;       /* pc */
    uint32_t cpsr;
} arm_thread_state64_t;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t entry_id_offset;
    uint32_t reserved;
    uint64_t vmaddr;
    uint64_t fileoff;
    uint64_t entry_offset;  /* Offset from vmaddr to entry point */
    uint32_t attr;
    uint32_t reserved2;
} fileset_entry_command_t;

/* ── Parsed Mach-O ─────────────────────────────── */
typedef struct {
    const uint8_t *data;
    size_t         size;

    mach_header_64_t header;

    /* Entry point (from LC_UNIXTHREAD or LC_FILESET_UNIXTHREAD) */
    uint64_t    entry_point;
    bool        has_entry;

    /* Kernel base address (lowest loaded segment) */
    uint64_t    base_addr;

    /* Filetype */
    bool        is_fileset;

    /* For MH_FILESET: the kernel core entry */
    struct {
        const uint8_t *data;
        size_t         size;
        uint64_t       vmaddr;
        uint64_t       entry_offset;
    } kernel_core;
} macho_context_t;

/* ── API ───────────────────────────────────────── */

/* Parse a Mach-O binary, extracting entry point and segment layout */
int macho_parse(macho_context_t *ctx, const uint8_t *data, size_t size);

/* Load a Mach-O binary into guest RAM.
 * Returns the entry point address, or 0 on failure.
 * If aslr_offset is non-zero, applies KASLR slide. */
uint64_t macho_load(macho_context_t *ctx, uint8_t *guest_ram,
                     uint64_t ram_base, uint64_t ram_size,
                     uint64_t aslr_offset);

/* For MH_FILESET: find the kernel core entry */
int macho_find_fileset_entry(macho_context_t *ctx, const char *entry_id,
                              macho_context_t *out_entry);

/* Get the arm_thread_state64 from LC_UNIXTHREAD */
bool macho_get_thread_state(macho_context_t *ctx, arm_thread_state64_t *state);

/* Free context */
void macho_free(macho_context_t *ctx);

#endif /* SILICONV_MACHO_H */
