/*
 * SiliconV — IMG4 Container Parser
 *
 * Parses Apple IMG4 firmware container format used for
 * kernel caches, DeviceTree blobs, and other firmware.
 *
 * IMG4 = IM4P (payload) + IM4M (manifest) + optional IM4R (restore)
 */

#ifndef SILICONV_IMG4_H
#define SILICONV_IMG4_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── IMG4 Magic ────────────────────────────────── */
#define IMG4_MAGIC  0x34474D49  /* "IMG4" */
#define IM4P_MAGIC  0x50344D49  /* "IM4P" */
#define IM4M_MAGIC  0x4D344D49  /* "IM4M" */
#define IM4R_MAGIC  0x52344D49  /* "IM4R" */

/* ── Payload Types ──────────────────────────────── */
typedef enum {
    IMG4_PAYLOAD_UNKNOWN = 0,
    IMG4_PAYLOAD_KRNL,     /* "krnl" — XNU kernel cache */
    IMG4_PAYLOAD_DTRE,     /* "dtre" — DeviceTree blob */
    IMG4_PAYLOAD_RDSK,     /* "rdsk" — RAM disk */
    IMG4_PAYLOAD_TRST,     /* "trst"/"rtsc" — TrustCache */
    IMG4_PAYLOAD_SEPF,     /* "sepf" — SEP firmware */
    IMG4_PAYLOAD_RAW,      /* No container, raw data */
} img4_payload_type_t;

/* ── IM4P Header ───────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* IM4P_MAGIC */
    uint32_t full_size;     /* Full IM4P size (header + data) */
    uint32_t data_offset;   /* Offset to payload data */
    uint32_t data_size;     /* Payload data size */
    char     type[4];       /* Payload type (e.g. "krnl") */
    uint8_t  reserved[4];
} im4p_header_t;

/* ── Parsed Payload ────────────────────────────── */
typedef struct {
    img4_payload_type_t type;
    char                type_str[5];  /* 4-char type + NUL */
    uint8_t            *data;
    uint32_t            data_size;
    bool                is_compressed;  /* LZSS or LZFSE */
    uint32_t            uncompressed_size;
} img4_payload_t;

/* ── IMG4 Container (parsed) ───────────────────── */
typedef struct {
    bool        valid;
    uint8_t    *raw_data;
    size_t      raw_size;
    bool        owns_data;      /* true if payload.data was allocated (needs free) */

    img4_payload_t payload;   /* IM4P payload */
    /* IM4M and IM4R are not parsed in detail but tracked for size */
    uint8_t    *manifest;
    uint32_t    manifest_size;
    uint8_t    *restore;
    uint32_t    restore_size;
} img4_container_t;

/* ── API ───────────────────────────────────────── */

/* Load and parse an IMG4 file (or raw binary) */
int img4_load(img4_container_t *img4, const uint8_t *data, size_t size);

/* Free a loaded IMG4 container */
void img4_free(img4_container_t *img4);

/* Check if data is an IMG4 container */
bool img4_is_container(const uint8_t *data, size_t size);

/* Decode payload type string to enum */
img4_payload_type_t img4_payload_type_from_str(const char type[4]);

/* Get human-readable payload type name */
const char* img4_payload_type_name(img4_payload_type_t type);

#endif /* SILICONV_IMG4_H */
