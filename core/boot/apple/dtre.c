/*
 * SiliconV — Apple DeviceTree (DTRE) Handler (Implementation)
 *
 * Loads, modifies, filters, and serializes Apple DeviceTree blobs.
 * Uses a custom binary format that is FDT-compatible at the header level
 * but with Apple-specific node structure.
 *
 * For the virtual platform, we support:
 *   - Loading DTB from IMG4 "dtre" payloads
 *   - Populating runtime info (memory, boot-args, random seeds)
 *   - Filtering out unimplemented devices
 *   - Serializing to standard FDT format for XNU consumption
 */

#include "dtre.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Supported device compatible strings ────────── */
static const char *apple_dt_keep_comp[] = {
    "aic,1",
    "aic,v1",
    "uart-1,samsung",
    "uart,samsung",
    "dart,s8000",
    "dart,t8010",
    "dart,t8020",
    "dart",
    "wdt",
    "nvram",
    "virtio,mmio",
    "apple-silicon",
    NULL
};

/* ── Properties to remove ──────────────────────── */
/* Used by apple_dt_filter_nodes to strip unsupported properties */
static const char *apple_dt_remove_props[] = {
    "content-protect",
    "encryptable",
    "pmp",
    "nand-debug",
    "nvme-coastguard",
    "baseband-chipset",
    NULL
};

/* ── Node creation ─────────────────────────────── */
apple_dt_node_t* apple_dt_node_new(const char *name)
{
    apple_dt_node_t *node = calloc(1, sizeof(apple_dt_node_t));
    if (node) {
        node->name = strdup(name ? name : "");
    }
    return node;
}

static apple_dt_prop_t* apple_dt_prop_new(const char *name,
                                            const void *value, uint32_t len)
{
    apple_dt_prop_t *prop = calloc(1, sizeof(apple_dt_prop_t));
    if (prop) {
        prop->name = strdup(name);
        prop->value = malloc(len ? len : 1);
        if (value && len > 0)
            memcpy(prop->value, value, len);
        prop->len = len;
    }
    return prop;
}

int apple_dt_set_prop(apple_dt_node_t *node, const char *name,
                       const void *value, uint32_t len)
{
    /* Check if property already exists — update it */
    apple_dt_prop_t *p = node->props;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            free(p->value);
            p->value = malloc(len ? len : 1);
            if (value && len > 0)
                memcpy(p->value, value, len);
            p->len = len;
            return 0;
        }
        p = p->next;
    }

    /* Create new property */
    apple_dt_prop_t *prop = apple_dt_prop_new(name, value, len);
    if (!prop) return -1;

    /* Add to end of list */
    if (!node->props) {
        node->props = prop;
    } else {
        p = node->props;
        while (p->next) p = p->next;
        p->next = prop;
    }
    return 0;
}

int apple_dt_set_prop_u32(apple_dt_node_t *node, const char *name, uint32_t val)
{
    uint32_t be_val = __builtin_bswap32(val);
    return apple_dt_set_prop(node, name, &be_val, 4);
}

int apple_dt_set_prop_u64(apple_dt_node_t *node, const char *name, uint64_t val)
{
    uint64_t be_val = __builtin_bswap64(val);
    return apple_dt_set_prop(node, name, &be_val, 8);
}

int apple_dt_set_prop_str(apple_dt_node_t *node, const char *name, const char *str)
{
    return apple_dt_set_prop(node, name, str, (uint32_t)(strlen(str) + 1));
}

int apple_dt_add_child(apple_dt_node_t *parent, apple_dt_node_t *child)
{
    if (!parent->children) {
        parent->children = child;
    } else {
        apple_dt_node_t *c = parent->children;
        while (c->next) c = c->next;
        c->next = child;
    }
    return 0;
}

/* ── Node search ───────────────────────────────── */
apple_dt_node_t* apple_dt_find_node(apple_dt_node_t *root, const char *path)
{
    if (!root || !path) return NULL;

    /* Skip leading '/' */
    if (*path == '/') path++;

    /* Empty path means root */
    if (*path == '\0') return root;

    /* Find first component */
    const char *slash = strchr(path, '/');
    size_t name_len = slash ? (size_t)(slash - path) : strlen(path);

    /* Search children */
    apple_dt_node_t *child = root->children;
    while (child) {
        if (strncmp(child->name, path, name_len) == 0 &&
            child->name[name_len] == '\0') {
            if (slash) {
                /* Recurse into sub-path */
                return apple_dt_find_node(child, slash + 1);
            }
            return child;
        }
        child = child->next;
    }

    return NULL;
}

apple_dt_prop_t* apple_dt_find_prop(apple_dt_node_t *node, const char *name)
{
    apple_dt_prop_t *p = node->props;
    while (p) {
        if (strcmp(p->name, name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

int apple_dt_remove_node(apple_dt_node_t *root, const char *path)
{
    if (!root || !path || *path == '\0') return -1;

    /* Skip leading '/' */
    if (*path == '/') path++;

    const char *slash = strchr(path, '/');
    size_t name_len = slash ? (size_t)(slash - path) : strlen(path);

    apple_dt_node_t *prev = NULL;
    apple_dt_node_t *child = root->children;

    while (child) {
        if (strncmp(child->name, path, name_len) == 0 &&
            child->name[name_len] == '\0') {
            if (slash) {
                /* Recurse */
                return apple_dt_remove_node(child, slash + 1);
            }

            /* Remove this node */
            if (prev)
                prev->next = child->next;
            else
                root->children = child->next;

            apple_dt_free(child);
            return 0;
        }
        prev = child;
        child = child->next;
    }

    return -1;
}

/* ── Node filtering ────────────────────────────── */
static bool node_should_keep(apple_dt_node_t *node)
{
    apple_dt_prop_t *comp = apple_dt_find_prop(node, "compatible");
    if (!comp) return true;  /* Keep nodes without compatible */

    /* Check if any compatible string matches our whitelist */
    const char **keep = apple_dt_keep_comp;
    while (*keep) {
        if (strstr((const char *)comp->value, *keep))
            return true;
        keep++;
    }

    return false;
}

/* ── Remove properties that are unsupported ────── */
static void dt_remove_unsupported_props(apple_dt_node_t *node)
{
    const char **rp = apple_dt_remove_props;
    while (*rp) {
        apple_dt_prop_t *prev = NULL;
        apple_dt_prop_t *p = node->props;
        while (p) {
            apple_dt_prop_t *next = p->next;
            if (strcmp(p->name, *rp) == 0) {
                if (prev)
                    prev->next = next;
                else
                    node->props = next;
                free(p->name);
                free(p->value);
                free(p);
            } else {
                prev = p;
            }
            p = next;
        }
        rp++;
    }
}

int apple_dt_filter_nodes(apple_dt_node_t *root)
{
    if (!root) return 0;

    /* Remove unsupported properties */
    dt_remove_unsupported_props(root);

    /* Filter children recursively */
    apple_dt_node_t *prev = NULL;
    apple_dt_node_t *child = root->children;

    while (child) {
        apple_dt_node_t *next = child->next;

        /* Recurse first */
        apple_dt_filter_nodes(child);

        /* Remove if not in whitelist */
        if (!node_should_keep(child)) {
            printf("dtre:   removing node '%s' (unimplemented device)\n", child->name);

            if (prev)
                prev->next = next;
            else
                root->children = next;

            apple_dt_free(child);
        } else {
            prev = child;
        }

        child = next;
    }

    return 0;
}

/* ── Serialization ─────────────────────────────── */
/* Serialize to standard FDT format (compatible with what XNU expects) */

/* FDT token definitions */
#define FDT_BEGIN_NODE    0x00000001
#define FDT_END_NODE      0x00000002
#define FDT_PROP          0x00000003
#define FDT_NOP           0x00000004
#define FDT_END           0x00000009

static int dt_node_serialize(apple_dt_node_t *node, uint8_t *buf,
                              uint32_t *struct_off, uint32_t *str_off,
                              uint32_t struct_size, uint32_t str_size)
{
    /* BEGIN_NODE */
    if (*struct_off + 4 > struct_size) return -1;
    *(uint32_t *)(buf + *struct_off) = __builtin_bswap32(FDT_BEGIN_NODE);
    *struct_off += 4;

    /* Node name (null-terminated, 4-byte aligned) */
    uint32_t name_len = (uint32_t)(strlen(node->name) + 1);
    uint32_t name_padded = (name_len + 3) & ~3;
    if (*struct_off + name_padded > struct_size) return -1;
    memcpy(buf + *struct_off, node->name, name_len);
    memset(buf + *struct_off + name_len, 0, name_padded - name_len);
    *struct_off += name_padded;

    /* Properties */
    apple_dt_prop_t *prop = node->props;
    while (prop) {
        if (!prop->placeholder) {
            /* PROP token */
            if (*struct_off + 4 > struct_size) return -1;
            *(uint32_t *)(buf + *struct_off) = __builtin_bswap32(FDT_PROP);
            *struct_off += 4;

            /* Property value length (big endian) */
            if (*struct_off + 4 > struct_size) return -1;
            *(uint32_t *)(buf + *struct_off) = __builtin_bswap32(prop->len);
            *struct_off += 4;

            /* String offset in strings block */
            if (*struct_off + 4 > struct_size) return -1;
            *(uint32_t *)(buf + *struct_off) = __builtin_bswap32(*str_off);
            *struct_off += 4;

            /* Copy property name to strings block */
            uint32_t pname_len = (uint32_t)(strlen(prop->name) + 1);
            if (*str_off + pname_len > str_size) return -1;
            memcpy(buf + *str_off, prop->name, pname_len);
            *str_off += pname_len;

            /* Copy property value (aligned) */
            uint32_t val_padded = (prop->len + 3) & ~3;
            if (*struct_off + val_padded > struct_size) return -1;
            if (prop->value && prop->len > 0)
                memcpy(buf + *struct_off, prop->value, prop->len);
            memset(buf + *struct_off + prop->len, 0, val_padded - prop->len);
            *struct_off += val_padded;
        }
        prop = prop->next;
    }

    /* Children */
    apple_dt_node_t *child = node->children;
    while (child) {
        if (dt_node_serialize(child, buf, struct_off, str_off,
                               struct_size, str_size) < 0)
            return -1;
        child = child->next;
    }

    /* END_NODE */
    if (*struct_off + 4 > struct_size) return -1;
    *(uint32_t *)(buf + *struct_off) = __builtin_bswap32(FDT_END_NODE);
    *struct_off += 4;

    return 0;
}

int apple_dt_serialize(apple_dt_node_t *root, uint8_t *buf, size_t buf_size)
{
    if (buf_size < sizeof(apple_dtb_header_t)) return -1;
    memset(buf, 0, buf_size);

    /* Pre-calculate sizes */
    uint32_t struct_size = 4096;   /* Reasonable default */
    uint32_t str_size = 2048;

    /* We'll use temp buffers for calculation */
    uint8_t *struct_buf = malloc(struct_size);
    uint8_t *str_buf = malloc(str_size);
    if (!struct_buf || !str_buf) {
        free(struct_buf);
        free(str_buf);
        return -1;
    }

    uint32_t struct_off = 0;
    uint32_t str_off = 0;

    /* Serialize the tree */
    if (dt_node_serialize(root, struct_buf, &struct_off, &str_off,
                           struct_size, str_size) < 0) {
        free(struct_buf);
        free(str_buf);
        return -1;
    }

    /* Pad to 16 bytes */
    while (struct_off & 15) {
        if (struct_off + 4 > struct_size) break;
        *(uint32_t *)(struct_buf + struct_off) = __builtin_bswap32(FDT_NOP);
        struct_off += 4;
    }

    /* FDT_END */
    if (struct_off + 4 > struct_size) {
        free(struct_buf);
        free(str_buf);
        return -1;
    }
    *(uint32_t *)(struct_buf + struct_off) = __builtin_bswap32(FDT_END);
    struct_off += 4;

    /* Build header */
    apple_dtb_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = __builtin_bswap32(0xEDFE0DD0); /* FDT magic (Apple variant) */
    hdr.version = __builtin_bswap32(17);
    hdr.last_comp_version = __builtin_bswap32(16);
    hdr.boot_cpuid_phys = 0;

    /* Calculate offsets (after header) */
    uint32_t off_mem_rsv = sizeof(apple_dtb_header_t);
    /* Memory reservation block: 2 x 8-bytes (one entry + terminator) = 16 bytes */
    off_mem_rsv = (off_mem_rsv + 7) & ~7;
    uint32_t off_struct_final = off_mem_rsv + 16;
    uint32_t off_str_final = off_struct_final + struct_off;
    off_str_final = (off_str_final + 3) & ~3;

    uint32_t total = off_str_final + str_off;
    total = (total + 15) & ~15;

    if (total > buf_size) {
        free(struct_buf);
        free(str_buf);
        return -1;
    }

    hdr.total_size = __builtin_bswap32(total);
    hdr.off_struct = __builtin_bswap32(off_struct_final);
    hdr.off_strings = __builtin_bswap32(off_str_final);
    hdr.off_reserve = __builtin_bswap32(off_mem_rsv);
    hdr.size_struct = __builtin_bswap32(struct_off);
    hdr.size_strings = __builtin_bswap32(str_off);

    /* Write header */
    memcpy(buf, &hdr, sizeof(hdr));

    /* Memory reservation block (empty = single terminator) */
    uint64_t zero = 0;
    memcpy(buf + off_mem_rsv, &zero, 8);
    memcpy(buf + off_mem_rsv + 8, &zero, 8);

    /* Structure block */
    memcpy(buf + off_struct_final, struct_buf, struct_off);

    /* Strings block */
    memcpy(buf + off_str_final, str_buf, str_off);

    free(struct_buf);
    free(str_buf);

    return (int)total;
}

/* ── Runtime population ────────────────────────── */
int apple_dt_populate_runtime(apple_dt_node_t *root,
                               uint64_t dram_base, uint64_t dram_size,
                               const char *boot_args)
{
    /* Find or create /chosen */
    apple_dt_node_t *chosen = apple_dt_find_node(root, "/chosen");
    if (!chosen) {
        chosen = apple_dt_node_new("chosen");
        if (!chosen) return -1;
        apple_dt_add_child(root, chosen);
    }

    /* Set boot-args */
    if (boot_args) {
        apple_dt_set_prop_str(chosen, "boot-args", boot_args);
    }

    /* Set memory info */
    apple_dt_set_prop_u64(chosen, "dram-base", dram_base);
    apple_dt_set_prop_u64(chosen, "dram-size", dram_size);

    /* Set random seed (placeholder — will be filled at finalize) */
    uint8_t seed[32] = {0};
    apple_dt_set_prop(chosen, "random-seed", seed, 32);
    apple_dt_set_prop(chosen, "boot-nonce", seed, 32);

    /* Set security domain (production, non-secure) */
    apple_dt_set_prop_u32(chosen, "effective-production-status-ap", 1);
    apple_dt_set_prop_u32(chosen, "effective-security-mode-ap", 0);
    apple_dt_set_prop_u32(chosen, "security-domain", 0);
    apple_dt_set_prop_u32(chosen, "chip-epoch", 0);
    apple_dt_set_prop_u32(chosen, "boot-command", 1); /* Normal boot */

    /* Set /chosen/manifest-properties */
    apple_dt_node_t *manifest = apple_dt_find_node(chosen, "manifest-properties");
    if (!manifest) {
        manifest = apple_dt_node_new("manifest-properties");
        if (manifest)
            apple_dt_add_child(chosen, manifest);
    }

    return 0;
}

int apple_dt_finalize(apple_dt_node_t *root)
{
    if (!root) return -1;
    root->finalized = true;

    apple_dt_node_t *child = root->children;
    while (child) {
        child->finalized = true;

        apple_dt_node_t *sibling = child->children;
        while (sibling) {
            sibling->finalized = true;
            sibling = sibling->next;
        }

        child = child->next;
    }

    return 0;
}

/* ── Debug: count nodes and properties ─────────── */
static void dt_count_node(apple_dt_node_t *node, int *nodes, int *props)
{
    (*nodes)++;
    apple_dt_prop_t *p = node->props;
    while (p) { (*props)++; p = p->next; }
    apple_dt_node_t *c = node->children;
    while (c) { dt_count_node(c, nodes, props); c = c->next; }
}

void apple_dt_print_stats(apple_dt_node_t *root)
{
    int nodes = 0, props = 0;
    dt_count_node(root, &nodes, &props);
    printf("dtre: %d nodes, %d properties\n", nodes, props);
}

/* ── Free ───────────────────────────────────────── */
void apple_dt_free(apple_dt_node_t *node)
{
    if (!node) return;

    /* Free properties */
    apple_dt_prop_t *prop = node->props;
    while (prop) {
        apple_dt_prop_t *next = prop->next;
        free(prop->name);
        free(prop->value);
        free(prop);
        prop = next;
    }

    /* Free children recursively */
    apple_dt_node_t *child = node->children;
    while (child) {
        apple_dt_node_t *next = child->next;
        apple_dt_free(child);
        child = next;
    }

    free(node->name);
    free(node);
}

/* ── FDT Token Parser (recursive descent) ───────── */

/* Parse a node from the structure block, advancing *offset.
 * Returns the parsed node, or NULL on error. */
static apple_dt_node_t* dt_parse_node(const uint8_t *struct_block,
                                       uint32_t struct_size,
                                       const uint8_t *str_block,
                                       uint32_t str_size,
                                       uint32_t *offset)
{
    if (*offset + 4 > struct_size) return NULL;

    uint32_t token = __builtin_bswap32(*(const uint32_t *)(struct_block + *offset));

    if (token != FDT_BEGIN_NODE) {
        fprintf(stderr, "dtre: expected BEGIN_NODE, got 0x%08X at offset %u\n",
                token, *offset);
        return NULL;
    }
    *offset += 4;

    /* Read node name (null-terminated string, 4-byte aligned) */
    if (*offset >= struct_size) return NULL;
    const char *name = (const char *)(struct_block + *offset);
    uint32_t name_len = (uint32_t)(strlen(name) + 1);
    uint32_t name_padded = (name_len + 3) & ~3;

    if (*offset + name_padded > struct_size) return NULL;

    apple_dt_node_t *node = apple_dt_node_new(name);
    if (!node) return NULL;

    *offset += name_padded;

    /* Parse properties and child nodes */
    while (*offset + 4 <= struct_size) {
        token = __builtin_bswap32(*(const uint32_t *)(struct_block + *offset));

        if (token == FDT_PROP) {
            /* Property */
            *offset += 4;

            if (*offset + 8 > struct_size) break;

            uint32_t prop_len = __builtin_bswap32(
                *(const uint32_t *)(struct_block + *offset));
            *offset += 4;

            uint32_t name_off = __builtin_bswap32(
                *(const uint32_t *)(struct_block + *offset));
            *offset += 4;

            /* Look up property name in strings block */
            if (name_off >= str_size) {
                fprintf(stderr, "dtre: property name offset %u exceeds strings block\n",
                        name_off);
                break;
            }
            const char *prop_name = (const char *)(str_block + name_off);

            /* Property value (padded to 4 bytes) */
            uint32_t val_padded = (prop_len + 3) & ~3;
            if (*offset + val_padded > struct_size) break;

            const void *prop_val = struct_block + *offset;
            *offset += val_padded;

            /* Add property to node */
            apple_dt_set_prop(node, prop_name, prop_val, prop_len);

        } else if (token == FDT_BEGIN_NODE) {
            /* Child node — recurse */
            apple_dt_node_t *child = dt_parse_node(struct_block, struct_size,
                                                    str_block, str_size, offset);
            if (child) {
                apple_dt_add_child(node, child);
            }

        } else if (token == FDT_END_NODE) {
            *offset += 4;
            return node;

        } else if (token == FDT_NOP) {
            *offset += 4;

        } else if (token == FDT_END) {
            /* End of structure block — treat as END_NODE for root */
            return node;

        } else {
            fprintf(stderr, "dtre: unknown token 0x%08X at offset %u\n",
                    token, *offset);
            break;
        }
    }

    return node;
}

/* ── Load from binary blob ─────────────────────── */
apple_dt_node_t* apple_dt_load(const uint8_t *data, size_t size)
{
    if (!data || size < sizeof(apple_dtb_header_t)) {
        fprintf(stderr, "dtre: invalid data (%zu bytes)\n", size);
        return NULL;
    }

    const apple_dtb_header_t *hdr = (const apple_dtb_header_t *)data;

    /* Verify FDT magic */
    uint32_t magic = __builtin_bswap32(hdr->magic);
    if (magic != 0xEDFE0DD0 && magic != 0xD00DFEED) {
        fprintf(stderr, "dtre: bad magic 0x%08X (expected FDT)\n", magic);
        return NULL;
    }

    uint32_t off_struct = __builtin_bswap32(hdr->off_struct);
    uint32_t off_strings = __builtin_bswap32(hdr->off_strings);
    uint32_t size_struct = __builtin_bswap32(hdr->size_struct);
    uint32_t str_size = __builtin_bswap32(hdr->size_strings);
    uint32_t total_size = __builtin_bswap32(hdr->total_size);

    /* Validate offsets */
    if (off_struct + size_struct > size || off_strings + str_size > size) {
        fprintf(stderr, "dtre: DTB offsets exceed file size\n");
        return NULL;
    }

    const uint8_t *struct_block = data + off_struct;
    const uint8_t *str_block = data + off_strings;

    printf("dtre: loaded DT blob (magic=0x%08X, total=%u, struct=%u/%u, strings=%u/%u)\n",
           magic, total_size,
           off_struct, size_struct,
           off_strings, str_size);

    /* Parse the root node */
    uint32_t offset = 0;
    apple_dt_node_t *root = dt_parse_node(struct_block, size_struct,
                                           str_block, str_size, &offset);

    if (!root) {
        fprintf(stderr, "dtre: failed to parse root node\n");
        return NULL;
    }

    printf("dtre: parsed tree — root \"%s\"\n", root->name);
    return root;
}
