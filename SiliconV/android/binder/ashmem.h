/*
 * SiliconV — Ashmem (Anonymous Shared Memory) Interface
 *
 * Ashmem is Android's shared memory driver.
 * Provides memory-backed file descriptors for IPC.
 *
 * In SiliconV: guest kernel has ashmem built-in.
 * The hypervisor doesn't need to emulate it — it's a kernel-internal driver.
 *
 * Reference: drivers/staging/android/ashmem.c
 */

#ifndef SILICONV_ASHMEM_H
#define SILICONV_ASHMEM_H

#include <stdint.h>

/* ── Ashmem ioctl Numbers ──────────────────────── */
#define __ASHMEMIOC         0x77

#define ASHMEM_SET_NAME     _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_SIZE])
#define ASHMEM_GET_NAME     _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_SIZE])
#define ASHMEM_SET_SIZE     _IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE     _IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN          _IO(__ASHMEMIOC, 7)
#define ASHMEM_UNPIN        _IO(__ASHMEMIOC, 8)
#define ASHMEM_GET_PIN_STATUS _IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES _IO(__ASHMEMIOC, 10)

#define ASHMEM_NAME_SIZE    256

/* ── Ashmem Region ─────────────────────────────── */
typedef struct {
    char     name[ASHMEM_NAME_SIZE];
    uint64_t size;
    uint32_t prot_mask;    /* PROT_READ | PROT_WRITE | PROT_EXEC */
    int      pin_count;
    int      fd;           /* File descriptor in guest */
} sv_ashmem_region_t;

/*
 * Kernel config required:
 *   CONFIG_ASHMEM=y
 *
 * Ashmem is self-contained in the guest kernel.
 * No hypervisor-side emulation needed.
 *
 * Device node: /dev/ashmem
 */

#endif /* SILICONV_ASHMEM_H */
