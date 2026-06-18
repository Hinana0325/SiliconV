/*
 * SiliconV — Apple DeviceTree (DTRE) Handler
 *
 * Processes Apple DeviceTree blobs (loaded from IMG4 "dtre" payloads).
 * Apple DT uses a custom serialization format similar to FDT but with
 * Apple-specific extensions.
 *
 * Key operations:
 *   1. Load and deserialize Apple DT blob
 *   2. Modify nodes/properties for runtime information
 *   3. Remove nodes for unimplemented devices
 *   4. Serialize back to FDT-compatible format for XNU
 */

#ifndef SILICONV_DTRE_H
#define SILICONV_DTRE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Apple DT Node ─────────────────────────────── */
typedef struct apple_dt_prop {
    char   *name;
    void   *value;
    uint32_t len;
    bool   placeholder;  /* Allocate space at finalize time */
    struct apple_dt_prop *next;
} apple_dt_prop_t;

typedef struct apple_dt_node {
    char   *name;
    apple_dt_prop_t *props;
    struct apple_dt_node *children;
    struct apple_dt_node *next;   /* Sibling linked list */
    bool   finalized;
} apple_dt_node_t;

/* ── Apple DTB serialization header ────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /* "dtre" or standard FDT magic */
    uint32_t total_size;
    uint32_t off_struct;
    uint32_t off_strings;
    uint32_t off_reserve;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_strings;
    uint32_t size_struct;
} apple_dtb_header_t;

/* ── API ───────────────────────────────────────── */

/* Load and deserialize an Apple DeviceTree blob */
apple_dt_node_t* apple_dt_load(const uint8_t *data, size_t size);

/* Create a new empty node */
apple_dt_node_t* apple_dt_node_new(const char *name);

/* Add a property to a node */
int apple_dt_set_prop(apple_dt_node_t *node, const char *name,
                       const void *value, uint32_t len);
int apple_dt_set_prop_u32(apple_dt_node_t *node, const char *name, uint32_t val);
int apple_dt_set_prop_u64(apple_dt_node_t *node, const char *name, uint64_t val);
int apple_dt_set_prop_str(apple_dt_node_t *node, const char *name, const char *str);

/* Add child node */
int apple_dt_add_child(apple_dt_node_t *parent, apple_dt_node_t *child);

/* Find a node by path (e.g., "/chosen", "/arm-io/aic") */
apple_dt_node_t* apple_dt_find_node(apple_dt_node_t *root, const char *path);

/* Find a property on a node */
apple_dt_prop_t* apple_dt_find_prop(apple_dt_node_t *node, const char *name);

/* Remove a node by path */
int apple_dt_remove_node(apple_dt_node_t *root, const char *path);

/* Remove nodes not in the supported device whitelist */
int apple_dt_filter_nodes(apple_dt_node_t *root);

/* Serialize the tree into FDT-compatible binary format.
 * Returns serialized size, or -1 on error. */
int apple_dt_serialize(apple_dt_node_t *root, uint8_t *buf, size_t buf_size);

/* Populate runtime data (boot-args, memory, etc.) */
int apple_dt_populate_runtime(apple_dt_node_t *root,
                               uint64_t dram_base, uint64_t dram_size,
                               const char *boot_args);

/* Finalize the tree (calculate sizes, freeze modifications) */
int apple_dt_finalize(apple_dt_node_t *root);

/* Free the entire tree */
void apple_dt_free(apple_dt_node_t *root);

/* Print statistics (for debugging) */
void apple_dt_print_stats(apple_dt_node_t *root);

#endif /* SILICONV_DTRE_H */
