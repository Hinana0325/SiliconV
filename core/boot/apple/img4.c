/*
 * SiliconV — IMG4 Container Parser (Implementation)
 *
 * Parses Apple IMG4 firmware container format used for
 * kernel caches, DeviceTree blobs, and other firmware.
 *
 * Supports:
 *   - Bare IMG4/IM4P containers (magic at offset 0)
 *   - DER-wrapped IM4P (ASN.1 SEQUENCE envelope)
 *   - LZFSE (bvx2) decompression via liblzfse
 *   - Raw binary fallback (no container found)
 */

#include "img4.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <lzfse.h>

/* Apple LZFSE compression magic */
#define BVX2_MAGIC  0x32787662  /* "bvx2" little-endian */

/* ── DER helper: parse ASN.1 length ───────────────── */
/* Returns the length value and advances *offset past the length field. */
static uint32_t der_read_length(const uint8_t *data, size_t size, size_t *offset)
{
    if (*offset >= size) return 0;
    uint8_t b = data[*offset]; (*offset)++;
    if (!(b & 0x80)) {
        /* Short form: length is b */
        return b;
    }
    /* Long form: bottom 7 bits = number of length bytes */
    uint32_t num = b & 0x7f;
    if (num > 4) return 0; /* too large for our simple parser */
    uint32_t length = 0;
    for (uint32_t i = 0; i < num; i++) {
        if (*offset >= size) return 0;
        length = (length << 8) | data[*offset]; (*offset)++;
    }
    return length;
}

/* ── DER helper: parse a SEQUENCE-wrapped IM4P ────── */
/*
 * Many Apple firmware files have an ASN.1 DER envelope:
 *   SEQUENCE {
 *     IA5String "IM4P"
 *     IA5String <type>           (e.g. "krnl", "dtre", "rdsk")
 *     IA5String <description>
 *     OCTET STRING <payload>     (may be bvx2-compressed)
 *   }
 *
 * Returns true and sets *payload_offset / *payload_size to
 * point to the OCTET STRING contents if found.
 */
static bool der_extract_im4p(const uint8_t *data, size_t size,
                              size_t *payload_offset,
                              uint32_t *payload_size)
{
    size_t off = 0;

    /* Expect SEQUENCE tag */
    if (off >= size || data[off] != 0x30) return false;
    off++;

    /* Read SEQUENCE length */
    uint32_t seq_len = der_read_length(data, size, &off);
    if (seq_len == 0 || off + seq_len > size) return false;

    /* Expect exactly 3 IA5Strings then 1 OCTET STRING */
    for (int elem = 0; elem < 4; elem++) {
        if (off >= size) return false;

        uint8_t tag = data[off]; off++;
        uint32_t len = der_read_length(data, size, &off);
        if (len == 0 || off + len > size) return false;

        if (elem == 0) {
            /* First IA5String must be "IM4P" */
            if (tag != 0x16 || len != 4 ||
                memcmp(data + off, "IM4P", 4) != 0)
                return false;
        } else if (elem == 1) {
            /* Second IA5String: payload type — captured but not stored here.
             * Must be 4 bytes. */
            if (tag != 0x16) return false;
        } else if (elem == 2) {
            /* Third IA5String: description — skip */
            if (tag != 0x16) return false;
        } else {
            /* Fourth element: OCTET STRING containing the payload */
            if (tag != 0x04) return false;
            *payload_offset = off;
            *payload_size = len;
            return true;
        }

        off += len;
    }

    return false;
}

/* ── LZFSE decompression helper ──────────────────── */
/*
 * Decompresses a bvx2-wrapped LZFSE buffer.
 * Returns a malloc'd buffer of decompressed data (or NULL on failure).
 * *out_size is set to the decompressed size.
 */
static uint8_t* decompress_bvx2(const uint8_t *compressed, size_t compressed_size,
                                 uint32_t *out_size)
{
    /* Check magic */
    if (compressed_size < 4) return NULL;
    uint32_t magic = *(const uint32_t *)compressed;
    if (magic != BVX2_MAGIC) return NULL;

    printf("img4: payload is LZFSE-compressed (bvx2, %zu bytes)\n", compressed_size);

    /* Compute a reasonable guess for uncompressed size.
     * For kernelcaches: ~3x compression ratio is typical.
     * We'll start aggressive and grow if needed. */
    size_t guess = compressed_size * 4;
    if (guess < 1024 * 1024) guess = 1024 * 1024;       /* minimum 1 MB */
    if (guess > 128 * 1024 * 1024) guess = 128 * 1024 * 1024; /* max 128 MB */

    uint8_t *decompressed = (uint8_t *)malloc(guess);
    if (!decompressed) return NULL;

    size_t result = lzfse_decode_buffer(decompressed, guess,
                                         compressed, compressed_size,
                                         NULL /* scratch — let library allocate */);

    if (result == 0) {
        printf("img4: LZFSE decode failed\n");
        free(decompressed);
        return NULL;
    }

    if (result >= guess) {
        /* Output was truncated — need larger buffer.
         * Double and retry (up to a limit). */
        size_t new_guess = guess * 2;
        if (new_guess > 256 * 1024 * 1024) {
            printf("img4: LZFSE output too large (>256 MB), giving up\n");
            free(decompressed);
            return NULL;
        }
        uint8_t *bigger = (uint8_t *)realloc(decompressed, new_guess);
        if (!bigger) { free(decompressed); return NULL; }
        decompressed = bigger;
        result = lzfse_decode_buffer(decompressed, new_guess,
                                      compressed, compressed_size,
                                      NULL);
        if (result == 0 || result >= new_guess) {
            printf("img4: LZFSE decode failed (retry)\n");
            free(decompressed);
            return NULL;
        }
        guess = new_guess;
    }

    printf("img4: LZFSE decompressed to %zu bytes\n", result);
    *out_size = (uint32_t)result;
    return decompressed;
}

/* ── Public API ──────────────────────────────────── */

img4_payload_type_t img4_payload_type_from_str(const char type[4])
{
    if (memcmp(type, "krnl", 4) == 0) return IMG4_PAYLOAD_KRNL;
    if (memcmp(type, "dtre", 4) == 0) return IMG4_PAYLOAD_DTRE;
    if (memcmp(type, "rdsk", 4) == 0) return IMG4_PAYLOAD_RDSK;
    if (memcmp(type, "trst", 4) == 0) return IMG4_PAYLOAD_TRST;
    if (memcmp(type, "rtsc", 4) == 0) return IMG4_PAYLOAD_TRST;
    if (memcmp(type, "sepf", 4) == 0) return IMG4_PAYLOAD_SEPF;
    return IMG4_PAYLOAD_UNKNOWN;
}

const char* img4_payload_type_name(img4_payload_type_t type)
{
    switch (type) {
    case IMG4_PAYLOAD_KRNL: return "kernel";
    case IMG4_PAYLOAD_DTRE: return "DeviceTree";
    case IMG4_PAYLOAD_RDSK: return "ramdisk";
    case IMG4_PAYLOAD_TRST: return "TrustCache";
    case IMG4_PAYLOAD_SEPF: return "SEP firmware";
    case IMG4_PAYLOAD_RAW:  return "raw binary";
    default:                return "unknown";
    }
}

bool img4_is_container(const uint8_t *data, size_t size)
{
    if (size < 8) return false;
    uint32_t magic = *(const uint32_t *)data;
    if (magic == IMG4_MAGIC || magic == IM4P_MAGIC) return true;
    /* Check for DER-wrapped IM4P (ASN.1 SEQUENCE containing "IM4P") */
    if (data[0] == 0x30) {
        /* Do a quick probe: look for "IM4P" at a reasonable offset */
        for (size_t i = 1; i < size && i < 32; i++) {
            if (data[i] == 'I' && i + 4 <= size &&
                memcmp(data + i, "IM4P", 4) == 0)
                return true;
        }
    }
    return false;
}

int img4_load(img4_container_t *img4, const uint8_t *data, size_t size)
{
    memset(img4, 0, sizeof(*img4));
    img4->owns_data = false;

    if (size < 8) {
        /* Too small — treat as raw binary */
        img4->payload.type = IMG4_PAYLOAD_RAW;
        img4->payload.data = (uint8_t *)data;
        img4->payload.data_size = (uint32_t)size;
        img4->valid = true;
        return 0;
    }

    uint32_t magic = *(const uint32_t *)data;

    /* ── Try DER-wrapped IM4P first ──────────────────────────── */
    if (data[0] == 0x30) {
        size_t payload_offset = 0;
        uint32_t payload_len = 0;

        if (der_extract_im4p(data, size, &payload_offset, &payload_len)) {
            const uint8_t *payload_data = data + payload_offset;

            /* Determine payload type from the second IA5String in DER.
             * Re-scan DER to extract it properly. */
            char type_str[5] = {0};
            /* Simple re-parse: find the second IA5String after "IM4P" */
            size_t off = 1;
            der_read_length(data, size, &off); /* skip sequence length */
            /* Skip first IA5String ("IM4P") */
            if (off < size && data[off] == 0x16) {
                off++;
                uint32_t slen = der_read_length(data, size, &off);
                off += slen;
                /* Read second IA5String (payload type) */
                if (off < size && data[off] == 0x16) {
                    off++;
                    slen = der_read_length(data, size, &off);
                    if (slen >= 4) {
                        memcpy(type_str, data + off, 4);
                        type_str[4] = '\0';
                    }
                }
            }

            printf("img4: DER-wrapped IM4P, type=%s, payload_len=%u\n",
                   type_str[0] ? type_str : "?", payload_len);

            /* Determine if payload is compressed (starts with bvx2) */
            if (payload_len >= 4 && *(const uint32_t *)payload_data == BVX2_MAGIC) {
                /* Decompress */
                uint32_t decomp_size = 0;
                uint8_t *decomp = decompress_bvx2(payload_data, payload_len, &decomp_size);
                if (decomp) {
                    img4->payload.data = decomp;
                    img4->payload.data_size = decomp_size;
                    img4->owns_data = true;
                    img4->payload.is_compressed = true;
                    img4->payload.uncompressed_size = decomp_size;
                } else {
                    fprintf(stderr, "img4: failed to decompress bvx2 payload\n");
                    return -1;
                }
            } else {
                /* Uncompressed payload */
                img4->payload.data = (uint8_t *)payload_data;
                img4->payload.data_size = payload_len;
                img4->payload.is_compressed = false;
            }

            if (type_str[0]) {
                img4->payload.type = img4_payload_type_from_str(type_str);
                memcpy(img4->payload.type_str, type_str, 4);
                img4->payload.type_str[4] = '\0';
            }

            img4->valid = true;
            img4->raw_data = (uint8_t *)data;
            img4->raw_size = size;

            printf("img4: loaded %s payload (%u bytes)%s\n",
                   img4_payload_type_name(img4->payload.type),
                   img4->payload.data_size,
                   img4->payload.is_compressed ? " (decompressed)" : "");
            return 0;
        }
    }

    /* ── IMG4 container ──────────────────────────────────────── */
    if (magic == IMG4_MAGIC) {
        /* IMG4 container: skip outer header, find IM4P/IM4M/IM4R */
        size_t offset = 8; /* Skip IMG4 magic + 4-byte length */

        while (offset + 8 < size) {
            uint32_t chunk_magic = *(const uint32_t *)(data + offset);
            uint32_t chunk_size  = *(const uint32_t *)(data + offset + 4);

            if (chunk_magic == IM4P_MAGIC) {
                /* Parse IM4P payload */
                const im4p_header_t *hdr = (const im4p_header_t *)(data + offset);
                img4->payload.type = img4_payload_type_from_str(hdr->type);
                memcpy(img4->payload.type_str, hdr->type, 4);
                img4->payload.type_str[4] = '\0';

                if (hdr->data_offset + hdr->data_size <= chunk_size) {
                    const uint8_t *payload_data = data + offset + hdr->data_offset;
                    uint32_t payload_len = hdr->data_size;

                    /* Check for bvx2 compression */
                    if (payload_len >= 4 &&
                        *(const uint32_t *)payload_data == BVX2_MAGIC) {
                        uint32_t decomp_size = 0;
                        uint8_t *decomp = decompress_bvx2(payload_data,
                                                           payload_len,
                                                           &decomp_size);
                        if (decomp) {
                            img4->payload.data = decomp;
                            img4->payload.data_size = decomp_size;
                            img4->owns_data = true;
                            img4->payload.is_compressed = true;
                            img4->payload.uncompressed_size = decomp_size;
                        } else {
                            fprintf(stderr, "img4: failed to decompress "
                                    "bvx2 payload in IMG4\n");
                            return -1;
                        }
                    } else {
                        img4->payload.data = (uint8_t *)payload_data;
                        img4->payload.data_size = payload_len;
                        img4->payload.is_compressed = false;
                    }
                }
            } else if (chunk_magic == IM4M_MAGIC) {
                img4->manifest = (uint8_t *)data + offset;
                img4->manifest_size = chunk_size;
            } else if (chunk_magic == IM4R_MAGIC) {
                img4->restore = (uint8_t *)data + offset;
                img4->restore_size = chunk_size;
            }

            offset += chunk_size;
        }

        img4->valid = (img4->payload.data != NULL);
        img4->raw_data = (uint8_t *)data;
        img4->raw_size = size;

        if (!img4->valid) {
            fprintf(stderr, "img4: no IM4P payload found in IMG4 container\n");
            return -1;
        }

        printf("img4: loaded %s payload (%u bytes)%s\n",
               img4_payload_type_name(img4->payload.type),
               img4->payload.data_size,
               img4->payload.is_compressed ? " (decompressed)" : "");
        return 0;

    /* ── Bare IM4P payload ───────────────────────────────────── */
    } else if (magic == IM4P_MAGIC) {
        const im4p_header_t *hdr = (const im4p_header_t *)data;
        img4->payload.type = img4_payload_type_from_str(hdr->type);
        memcpy(img4->payload.type_str, hdr->type, 4);
        img4->payload.type_str[4] = '\0';

        if (hdr->data_offset + hdr->data_size <= size) {
            const uint8_t *payload_data = data + hdr->data_offset;
            uint32_t payload_len = hdr->data_size;

            /* Check for bvx2 compression */
            if (payload_len >= 4 &&
                *(const uint32_t *)payload_data == BVX2_MAGIC) {
                uint32_t decomp_size = 0;
                uint8_t *decomp = decompress_bvx2(payload_data,
                                                   payload_len,
                                                   &decomp_size);
                if (decomp) {
                    img4->payload.data = decomp;
                    img4->payload.data_size = decomp_size;
                    img4->owns_data = true;
                    img4->payload.is_compressed = true;
                    img4->payload.uncompressed_size = decomp_size;
                } else {
                    fprintf(stderr, "img4: failed to decompress "
                            "bvx2 payload in IM4P\n");
                    return -1;
                }
            } else {
                img4->payload.data = (uint8_t *)payload_data;
                img4->payload.data_size = payload_len;
                img4->payload.is_compressed = false;
            }
        }
        img4->valid = (img4->payload.data != NULL);
        img4->raw_data = (uint8_t *)data;
        img4->raw_size = size;

        printf("img4: loaded bare %s payload (%u bytes)%s\n",
               img4_payload_type_name(img4->payload.type),
               img4->payload.data_size,
               img4->payload.is_compressed ? " (decompressed)" : "");
        return 0;
    }

    /* ── Not a container — treat as raw binary ───────────────── */
    img4->payload.type = IMG4_PAYLOAD_RAW;
    img4->payload.data = (uint8_t *)data;
    img4->payload.data_size = (uint32_t)size;
    img4->payload.is_compressed = false;
    img4->valid = true;

    printf("img4: loaded as raw binary (%u bytes)\n", img4->payload.data_size);
    return 0;
}

void img4_free(img4_container_t *img4)
{
    if (img4->owns_data && img4->payload.data) {
        free(img4->payload.data);
        img4->payload.data = NULL;
    }
    memset(img4, 0, sizeof(*img4));
}
