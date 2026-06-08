/*
 * SiliconV — DTB Generator (Implementation)
 *
 * Generates FDT binary directly — no libfdt dependency.
 *
 * FDT binary format:
 *   - Header (40 bytes)
 *   - Memory reservation block
 *   - Structure block (tokenized nodes)
 *   - Strings block (property names)
 */

#include "dtb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── FDT Tokens ────────────────────────────────── */
#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9

/* ── FDT Header ────────────────────────────────── */
typedef struct {
    uint32_t magic;           /* 0xd00dfeed */
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;         /* 17 */
    uint32_t last_comp_version; /* 16 */
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} __attribute__((packed)) fdt_header_t;

/* ── Writer State ──────────────────────────────── */
typedef struct {
    uint8_t *buf;
    size_t   bufsize;
    size_t   pos;       /* Current position in struct block */
    size_t   stroff;    /* Current offset in strings block */
    size_t   strstart;  /* Start of strings block */
    int      depth;     /* Nesting depth */
} dtb_writer_t;

static void w_init(dtb_writer_t *w, uint8_t *buf, size_t bufsize)
{
    w->buf = buf;
    w->bufsize = bufsize;
    w->pos = 0;
    w->stroff = 0;
    w->strstart = 0;
    w->depth = 0;
}

/* Align position to 4 bytes */
static void w_align4(dtb_writer_t *w)
{
    while (w->pos & 3) {
        w->buf[w->pos++] = 0;
    }
}

/* Write a 32-bit big-endian value */
static void w_u32(dtb_writer_t *w, uint32_t val)
{
    w->buf[w->pos++] = (val >> 24) & 0xFF;
    w->buf[w->pos++] = (val >> 16) & 0xFF;
    w->buf[w->pos++] = (val >> 8) & 0xFF;
    w->buf[w->pos++] = val & 0xFF;
}

/* Write raw bytes */
static void w_bytes(dtb_writer_t *w, const void *data, size_t len)
{
    memcpy(w->buf + w->pos, data, len);
    w->pos += len;
}

/* Write a string (null-terminated) */
static void w_string(dtb_writer_t *w, const char *s)
{
    size_t len = strlen(s) + 1;
    memcpy(w->buf + w->pos, s, len);
    w->pos += len;
}

/* Add a string to the strings block, return its offset */
static uint32_t add_string(dtb_writer_t *w, const char *s)
{
    size_t len = strlen(s) + 1;
    size_t off = w->stroff;
    memcpy(w->buf + w->strstart + w->stroff, s, len);
    w->stroff += len;
    return off;
}

/* Write FDT_BEGIN_NODE token */
static void w_begin_node(dtb_writer_t *w, const char *name)
{
    w_u32(w, FDT_BEGIN_NODE);
    w_string(w, name ? name : "");
    w_align4(w);
    w->depth++;
}

/* Write FDT_END_NODE token */
static void w_end_node(dtb_writer_t *w)
{
    w_u32(w, FDT_END_NODE);
    w->depth--;
}

/* Write a property */
static void w_prop(dtb_writer_t *w, const char *name,
                   const void *data, uint32_t len)
{
    w_u32(w, FDT_PROP);
    w_u32(w, len);
    w_u32(w, add_string(w, name));
    if (len > 0) {
        w_bytes(w, data, len);
        w_align4(w);
    }
}

/* Write a u32 property */
static void w_prop_u32(dtb_writer_t *w, const char *name, uint32_t val)
{
    uint32_t be = ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
                  ((val & 0xFF00) << 8) | ((val & 0xFF) << 24);
    w_prop(w, name, &be, 4);
}

/* Write a #address-cells / #size-cells style u64+u64 property */
static void w_prop_reg2(dtb_writer_t *w, const char *name,
                        uint64_t addr, uint64_t size)
{
    uint8_t data[16];
    /* #address-cells=2, #size-cells=2 */
    data[0] = (addr >> 56) & 0xFF; data[1] = (addr >> 48) & 0xFF;
    data[2] = (addr >> 40) & 0xFF; data[3] = (addr >> 32) & 0xFF;
    data[4] = (addr >> 24) & 0xFF; data[5] = (addr >> 16) & 0xFF;
    data[6] = (addr >> 8) & 0xFF;  data[7] = addr & 0xFF;
    data[8] = (size >> 56) & 0xFF; data[9] = (size >> 48) & 0xFF;
    data[10] = (size >> 40) & 0xFF; data[11] = (size >> 32) & 0xFF;
    data[12] = (size >> 24) & 0xFF; data[13] = (size >> 16) & 0xFF;
    data[14] = (size >> 8) & 0xFF;  data[15] = size & 0xFF;
    w_prop(w, name, data, 16);
}

/* Write a string property */
static void w_prop_str(dtb_writer_t *w, const char *name, const char *val)
{
    w_prop(w, name, val, strlen(val) + 1);
}

/* Write a u32 list property (big-endian array) */
static void w_prop_u32list(dtb_writer_t *w, const char *name,
                           const uint32_t *vals, int count)
{
    uint8_t data[count * 4];
    for (int i = 0; i < count; i++) {
        uint32_t v = vals[i];
        data[i*4+0] = (v >> 24) & 0xFF;
        data[i*4+1] = (v >> 16) & 0xFF;
        data[i*4+2] = (v >> 8) & 0xFF;
        data[i*4+3] = v & 0xFF;
    }
    w_prop(w, name, data, count * 4);
}

/* Write empty property (just presence) */
static void w_prop_empty(dtb_writer_t *w, const char *name)
{
    w_prop(w, name, NULL, 0);
}

/* ── DTB Generator ─────────────────────────────── */

int dtb_generate(uint8_t *buf, size_t bufsize, const dtb_config_t *config)
{
    if (bufsize < 4096) return -1;

    dtb_writer_t w;
    w_init(&w, buf, bufsize);

    /* Reserve space for header (will fill in later) */
    size_t header_size = sizeof(fdt_header_t);
    /* Align header to 8 bytes */
    while (header_size & 7) header_size++;
    w.pos = header_size;

    /* Memory reservation block (empty, terminator) */
    w_u32(&w, 0); w_u32(&w, 0);  /* No reservations */
    w_u32(&w, 0); w_u32(&w, 0);  /* Terminator */

    /* Structure block starts here */
    size_t struct_start = w.pos;

    /* Strings block will be after struct block — we'll set strstart later */
    /* For now, we write strings into a temporary area and move them later */
    /* Actually, let's estimate: strings block ~2KB, leave room */
    w.strstart = bufsize - 2048;
    w.stroff = 0;

    /* ── Root Node ─────────────────────────────── */
    w_begin_node(&w, "");
    w_prop_str(&w, "compatible", "siliconv,vm-v0");
    w_prop_str(&w, "model", "SiliconV Virtual Machine");
    w_prop_u32(&w, "#address-cells", 2);
    w_prop_u32(&w, "#size-cells", 2);
    w_prop_u32(&w, "interrupt-parent", 1);

    /* ── chosen ────────────────────────────────── */
    w_begin_node(&w, "chosen");
    if (config->cmdline)
        w_prop_str(&w, "bootargs", config->cmdline);
    w_prop_str(&w, "stdout-path", "/uart@10000000");
    w_end_node(&w);

    /* ── memory ────────────────────────────────── */
    char mem_name[32];
    snprintf(mem_name, sizeof(mem_name), "memory@%lx",
             (unsigned long)config->ram_base);
    w_begin_node(&w, mem_name);
    w_prop_str(&w, "device_type", "memory");
    w_prop_reg2(&w, "reg", config->ram_base, config->ram_size);
    w_end_node(&w);

    /* ── cpus ──────────────────────────────────── */
    w_begin_node(&w, "cpus");
    w_prop_u32(&w, "#address-cells", 1);
    w_prop_u32(&w, "#size-cells", 0);

    for (int i = 0; i < config->num_cpus; i++) {
        char cpu_name[16];
        snprintf(cpu_name, sizeof(cpu_name), "cpu@%d", i);
        w_begin_node(&w, cpu_name);
        w_prop_str(&w, "device_type", "cpu");
        w_prop_str(&w, "compatible", "arm,cortex-a55");
        w_prop_u32(&w, "reg", i);
        w_prop_str(&w, "enable-method", "psci");
        w_end_node(&w);
    }
    w_end_node(&w); /* cpus */

    /* ── psci ──────────────────────────────────── */
    w_begin_node(&w, "psci");
    w_prop_str(&w, "compatible", "arm,psci-1.0");
    w_prop_str(&w, "method", "hvc");
    w_end_node(&w);

    /* ── timer ─────────────────────────────────── */
    w_begin_node(&w, "timer");
    w_prop_str(&w, "compatible", "arm,armv8-timer");
    /* PPI 13,14,11,10 - all CPUs, GIC_CPU_MASK_SIMPLE(4) | IRQ_TYPE_LEVEL_LOW */
    uint32_t timer_irqs[] = {
        1, 13, 0x000F0008,  /* PPI 13, CPU mask 0xF, level low */
        1, 14, 0x000F0008,  /* PPI 14 */
        1, 11, 0x000F0008,  /* PPI 11 */
        1, 10, 0x000F0008,  /* PPI 10 */
    };
    w_prop_u32list(&w, "interrupts", timer_irqs, 12);
    w_end_node(&w);

    /* ── GICv3 ─────────────────────────────────── */
    w_begin_node(&w, "interrupt-controller@8000000");
    w_prop_str(&w, "compatible", "arm,gic-v3");
    w_prop_u32(&w, "#interrupt-cells", 3);
    w_prop_u32(&w, "#address-cells", 2);
    w_prop_u32(&w, "#size-cells", 2);
    w_prop_empty(&w, "interrupt-controller");
    w_prop_u32(&w, "phandle", 1);
    uint8_t gic_regdata[48] = {
        /* GICD: 0x00000000 0x08000000 0x00000000 0x00010000 */
        0,0,0,0, 0x08,0,0,0,  0,0,0,0, 0,1,0,0,
        /* GICR: 0x00000000 0x08010000 0x00000000 0x00010000 */
        0,0,0,0, 0x08,0x01,0,0, 0,0,0,0, 0,1,0,0,
        /* GITS: 0x00000000 0x08020000 0x00000000 0x00010000 */
        0,0,0,0, 0x08,0x02,0,0, 0,0,0,0, 0,1,0,0,
    };
    w_prop(&w, "reg", gic_regdata, 48);
    w_prop_empty(&w, "ranges");
    w_end_node(&w);

    /* ── UART (PL011) ──────────────────────────── */
    w_begin_node(&w, "uart@10000000");
    w_prop(&w, "compatible", "arm,pl011\0arm,primecell", 24);
    w_prop_reg2(&w, "reg", 0x10000000, 0x10000);
    uint32_t uart_irq[] = { 0, 32, 4 };
    w_prop_u32list(&w, "interrupts", uart_irq, 3);
    w_prop_u32(&w, "clock-frequency", 24000000);
    w_prop_str(&w, "status", "okay");
    w_end_node(&w);

    /* ── Virtio Devices ────────────────────────── */
    for (int i = 0; i < config->num_virtio; i++) {
        char name[32];
        snprintf(name, sizeof(name), "virtio_mmio@%lx",
                 (unsigned long)config->virtio[i].base);
        w_begin_node(&w, name);
        w_prop_str(&w, "compatible", "virtio,mmio");
        w_prop_reg2(&w, "reg", config->virtio[i].base, 0x200);
        uint32_t virq[] = { 0, (uint32_t)config->virtio[i].irq, 4 };
        w_prop_u32list(&w, "interrupts", virq, 3);
        w_end_node(&w);
    }

    /* ── End Root ──────────────────────────────── */
    w_end_node(&w);
    w_u32(&w, FDT_END);

    /* ── Assemble Header ───────────────────────── */
    size_t struct_size = w.pos - struct_start;
    size_t strings_size = w.stroff;
    size_t total_size = w.strstart + strings_size;

    /* Align total size */
    while (total_size & 3) total_size++;

    fdt_header_t *hdr = (fdt_header_t*)buf;
    hdr->magic = 0xd00dfeed;
    hdr->totalsize = total_size;
    hdr->off_dt_struct = struct_start;
    hdr->off_dt_strings = w.strstart;
    hdr->off_mem_rsvmap = header_size;
    hdr->version = 17;
    hdr->last_comp_version = 16;
    hdr->boot_cpuid_phys = 0;
    hdr->size_dt_strings = strings_size;
    hdr->size_dt_struct = struct_size;

    /* Byte-swap header fields to big-endian */
    uint32_t *fields = (uint32_t*)hdr;
    for (int i = 0; i < 10; i++) {
        uint32_t v = fields[i];
        fields[i] = ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                    ((v & 0xFF00) << 8) | ((v & 0xFF) << 24);
    }

    return total_size;
}

dtb_config_t dtb_config_default(int num_cpus, uint64_t ram_size)
{
    dtb_config_t cfg = {
        .num_cpus = num_cpus,
        .ram_base = 0x400000000ULL,
        .ram_size = ram_size,
        .uart_base = 0x10000000,
        .uart_irq = 32,
        .virtio = {
            { 0x20000000, 40, 2 },  /* virtio-blk */
            { 0x20010000, 41, 1 },  /* virtio-net */
            { 0x20020000, 42, 18 }, /* virtio-input */
            { 0x20030000, 43, 16 }, /* virtio-gpu */
            { 0x20040000, 44, 3 },  /* virtio-console */
        },
        .num_virtio = 5,
        .cmdline = "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw",
    };
    return cfg;
}
