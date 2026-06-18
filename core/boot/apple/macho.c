/*
 * SiliconV — Mach-O Loader for XNU Kernel (Implementation)
 *
 * Loads XNU kernel Mach-O binaries into guest RAM.
 * Supports both traditional MH_EXECUTE and modern MH_FILESET formats.
 */

#include "macho.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool macho_swap = false;

static uint32_t r32(const uint32_t *p) {
    return macho_swap ? __builtin_bswap32(*p) : *p;
}
static uint64_t r64(const uint64_t *p) {
    return macho_swap ? __builtin_bswap64(*p) : *p;
}

int macho_parse(macho_context_t *ctx, const uint8_t *data, size_t size)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->data = data;
    ctx->size = size;

    if (size < sizeof(mach_header_64_t)) {
        fprintf(stderr, "macho: file too small (%zu bytes)\n", size);
        return -1;
    }

    const mach_header_64_t *hdr = (const mach_header_64_t *)data;

    /* Detect byte swap */
    if (hdr->magic == MH_MAGIC_64) {
        macho_swap = false;
    } else if (hdr->magic == MH_CIGAM_64) {
        macho_swap = true;
    } else {
        fprintf(stderr, "macho: bad magic 0x%08X\n", hdr->magic);
        return -1;
    }

    ctx->header = *hdr;
    ctx->header.magic = r32(&hdr->magic);
    ctx->header.cputype = r32(&hdr->cputype);
    ctx->header.cpusubtype = r32(&hdr->cpusubtype);
    ctx->header.filetype = r32(&hdr->filetype);
    ctx->header.ncmds = r32(&hdr->ncmds);
    ctx->header.sizeofcmds = r32(&hdr->sizeofcmds);
    ctx->header.flags = r32(&hdr->flags);

    ctx->is_fileset = (ctx->header.filetype == MH_FILESET);

    /* Walk load commands */
    uint32_t offset = sizeof(mach_header_64_t);
    uint64_t lowest_addr = ~0ULL;

    for (uint32_t i = 0; i < ctx->header.ncmds; i++) {
        if (offset + sizeof(uint32_t) * 2 > size) {
            fprintf(stderr, "macho: truncated load commands\n");
            return -1;
        }

        const uint32_t *cmd_data = (const uint32_t *)(data + offset);
        uint32_t cmd = r32(&cmd_data[0]);
        uint32_t cmdsize = r32(&cmd_data[1]);

        if (cmdsize < 8 || offset + cmdsize > size) {
            fprintf(stderr, "macho: invalid cmdsize %u\n", cmdsize);
            return -1;
        }

        switch (cmd) {
        case LC_SEGMENT_64: {
            const segment_command_64_t *seg = (const segment_command_64_t *)(data + offset);
            if (seg->filesize > 0) {
                uint64_t vmaddr = r64(&seg->vmaddr);
                if (vmaddr < lowest_addr)
                    lowest_addr = vmaddr;
            }
            break;
        }

        case LC_UNIXTHREAD: {
            const thread_command_t *thr = (const thread_command_t *)(data + offset);
            uint32_t flavor = r32(&thr->flavor);
            uint32_t count = r32(&thr->count);

            if (flavor == ARM_THREAD_STATE64) {
                const arm_thread_state64_t *state =
                    (const arm_thread_state64_t *)(data + offset + sizeof(thread_command_t));
                ctx->entry_point = r64(&state->pc);
                ctx->has_entry = true;
            }
            break;
        }

        case LC_FILESET_ENTRY: {
            const fileset_entry_command_t *fe = (const fileset_entry_command_t *)(data + offset);
            uint64_t vmaddr = r64(&fe->vmaddr);
            if (vmaddr < lowest_addr)
                lowest_addr = vmaddr;
            break;
        }
        }

        offset += cmdsize;
    }

    ctx->base_addr = (lowest_addr != ~0ULL) ? lowest_addr : 0;

    printf("macho: parsed %s (%s), entry=0x%lx, base=0x%lx, %u commands\n",
           ctx->is_fileset ? "MH_FILESET" : "MH_EXECUTE",
           ctx->has_entry ? "has entry" : "no entry",
           (unsigned long)ctx->entry_point,
           (unsigned long)ctx->base_addr,
           ctx->header.ncmds);

    return 0;
}

uint64_t macho_load(macho_context_t *ctx, uint8_t *guest_ram,
                     uint64_t ram_base, uint64_t ram_size,
                     uint64_t aslr_offset)
{
    const uint8_t *data = ctx->data;
    size_t size = ctx->size;
    uint32_t offset = sizeof(mach_header_64_t);

    /* For MH_FILESET, we only load the kernel core entry */
    if (ctx->is_fileset) {
        if (ctx->kernel_core.data) {
            printf("macho: loading fileset kernel core at vmaddr 0x%lx\n",
                   (unsigned long)ctx->kernel_core.vmaddr);
            /* Fall through to load kernel_core as MH_EXECUTE */
            macho_context_t core_ctx;
            if (macho_parse(&core_ctx, ctx->kernel_core.data, ctx->kernel_core.size) == 0) {
                uint64_t ret = macho_load(&core_ctx, guest_ram, ram_base, ram_size, aslr_offset);
                macho_free(&core_ctx);
                return ret;
            }
            return 0;
        }
        fprintf(stderr, "macho: MH_FILESET but no kernel core selected\n");
        return 0;
    }

    uint64_t entry = ctx->entry_point;
    uint64_t base_addr = ctx->base_addr;

    for (uint32_t i = 0; i < ctx->header.ncmds; i++) {
        if (offset + sizeof(uint32_t) * 2 > size) break;

        const uint32_t *cmd_data = (const uint32_t *)(data + offset);
        uint32_t cmd = r32(&cmd_data[0]);
        uint32_t cmdsize = r32(&cmd_data[1]);

        if (cmd == LC_SEGMENT_64) {
            const segment_command_64_t *seg = (const segment_command_64_t *)(data + offset);
            uint64_t seg_vmaddr = r64(&seg->vmaddr);
            uint64_t vmsize = r64(&seg->vmsize);
            uint64_t fileoff = r64(&seg->fileoff);
            uint64_t filesize = r64(&seg->filesize);

            /* Skip placeholder segments with no file data */
            if (filesize == 0 && vmsize == 0) {
                offset += cmdsize;
                continue;
            }

            /* Skip segments with vmaddr below base_addr (not part of kernel mapping) */
            if (seg_vmaddr < base_addr) {
                offset += cmdsize;
                continue;
            }

            /*
             * Kernel images use virtual addresses in the high kernel space
             * (e.g. 0xfffffff0xxxxxxxx). We place the kernel data at a buffer
             * offset relative to the kernel's base address, then add ASLR:
             *
             *   buf_offset = (seg_vmaddr - base_addr) + aslr_offset
             *
             * This maps the kernel's virtual address space into guest RAM
             * starting at a small offset (base_relative + aslr).
             *
             * The returned entry point is a guest physical address:
             *   ram_base + (entry_point - base_addr) + aslr_offset
             */
            uint64_t buf_offset = (seg_vmaddr - base_addr) + aslr_offset;

            if (buf_offset + vmsize > ram_size) {
                fprintf(stderr, "macho: segment %s (buf 0x%lx+0x%lx) exceeds guest RAM\n",
                       seg->segname,
                       (unsigned long)buf_offset,
                       (unsigned long)vmsize);
                offset += cmdsize;
                continue;
            }

            if (filesize > 0) {
                /* Copy segment data from file into guest RAM buffer */
                memcpy(guest_ram + buf_offset, data + fileoff, filesize);
                printf("macho:   loaded %s at buf 0x%lx (guest phys 0x%lx, %lu bytes, vmsize 0x%lx)\n",
                       seg->segname,
                       (unsigned long)buf_offset,
                       (unsigned long)(ram_base + buf_offset),
                       (unsigned long)filesize,
                       (unsigned long)vmsize);

                /* Zero-fill the remainder (BSS) */
                if (vmsize > filesize) {
                    memset(guest_ram + buf_offset + filesize, 0, vmsize - filesize);
                }
            }

            /* Adjust entry if it falls within this segment */
            if (ctx->has_entry &&
                ctx->entry_point >= seg_vmaddr &&
                ctx->entry_point < seg_vmaddr + vmsize) {
                /* Entry = ram_base + (entry_point - base_addr) + aslr */
                entry = ram_base + (ctx->entry_point - base_addr) + aslr_offset;
            }
        }

        offset += cmdsize;
    }

    printf("macho: kernel loaded, entry at 0x%lx (guest phys)\n", (unsigned long)entry);
    return entry;
}

int macho_find_fileset_entry(macho_context_t *ctx, const char *entry_id,
                              macho_context_t *out_entry)
{
    memset(out_entry, 0, sizeof(*out_entry));

    if (!ctx->is_fileset) {
        fprintf(stderr, "macho: not a fileset\n");
        return -1;
    }

    const uint8_t *data = ctx->data;
    size_t size = ctx->size;
    uint32_t offset = sizeof(mach_header_64_t);

    for (uint32_t i = 0; i < ctx->header.ncmds; i++) {
        if (offset + sizeof(uint32_t) * 2 > size) break;

        const uint32_t *cmd_data = (const uint32_t *)(data + offset);
        uint32_t cmd = r32(&cmd_data[0]);
        uint32_t cmdsize = r32(&cmd_data[1]);

        if (cmd == LC_FILESET_ENTRY) {
            const fileset_entry_command_t *fe = (const fileset_entry_command_t *)(data + offset);
            const char *entry_name = (const char *)(data + offset + r32(&fe->entry_id_offset));

            if (entry_id == NULL || strcmp(entry_name, entry_id) == 0) {
                uint64_t fileoff = r64(&fe->fileoff);
                uint64_t vmaddr = r64(&fe->vmaddr);
                uint32_t attr = r32(&fe->attr);

                out_entry->data = data + fileoff;
                out_entry->size = size - (size_t)fileoff;
                out_entry->kernel_core.vmaddr = vmaddr;
                out_entry->kernel_core.entry_offset = r64(&fe->entry_offset);

                printf("macho: found fileset entry '%s' at fileoff 0x%lx, vmaddr 0x%lx\n",
                       entry_name ? entry_name : "?",
                       (unsigned long)fileoff,
                       (unsigned long)vmaddr);

                /* Parse it as a Mach-O to get entry point */
                return macho_parse(out_entry, out_entry->data, out_entry->size);
            }
        }

        offset += cmdsize;
    }

    fprintf(stderr, "macho: fileset entry '%s' not found\n", entry_id ? entry_id : "(null)");
    return -1;
}

bool macho_get_thread_state(macho_context_t *ctx, arm_thread_state64_t *state)
{
    memset(state, 0, sizeof(*state));

    const uint8_t *data = ctx->data;
    size_t size = ctx->size;
    uint32_t offset = sizeof(mach_header_64_t);

    for (uint32_t i = 0; i < ctx->header.ncmds; i++) {
        if (offset + sizeof(uint32_t) * 2 > size) break;

        const uint32_t *cmd_data = (const uint32_t *)(data + offset);
        uint32_t cmd = r32(&cmd_data[0]);
        uint32_t cmdsize = r32(&cmd_data[1]);

        if (cmd == LC_UNIXTHREAD) {
            const thread_command_t *thr = (const thread_command_t *)(data + offset);
            uint32_t flavor = r32(&thr->flavor);
            if (flavor == ARM_THREAD_STATE64) {
                memcpy(state, data + offset + sizeof(thread_command_t),
                       sizeof(arm_thread_state64_t));
                return true;
            }
        }

        offset += cmdsize;
    }

    return false;
}

void macho_free(macho_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}
