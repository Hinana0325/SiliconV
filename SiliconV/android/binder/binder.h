/*
 * SiliconV — Binder Driver Interface
 *
 * Binder is Android's IPC mechanism. For SiliconV:
 *   - Guest kernel has binder built-in (CONFIG_ANDROID_BINDER_DEVICES)
 *   - Hypervisor exposes /dev/binder, /dev/hwbinder, /dev/vndbinder
 *   - Optional: virtio-binder for host↔guest IPC
 *
 * Reference: drivers/android/binder.c (Linux kernel)
 */

#ifndef SILICONV_BINDER_H
#define SILICONV_BINDER_H

#include <stdint.h>
#include <stdbool.h>

/* ── Binder Device Types ───────────────────────── */
#define BINDER_DEV_BINDER     0   /* /dev/binder (framework) */
#define BINDER_DEV_HWBINDER   1   /* /dev/hwbinder (HAL) */
#define BINDER_DEV_VNDBINDER  2   /* /dev/vndbinder (vendor) */

/* ── Binder Protocol Constants ─────────────────── */

/* Transaction codes */
#define BC_TRANSACTION         _IOW('b', 0, struct binder_transaction_data)
#define BC_REPLY               _IOW('b', 1, struct binder_transaction_data)
#define BC_FREE_BUFFER         _IOW('b', 3, binder_uintptr_t)
#define BC_INCREFS             _IOW('b', 4, __u32)
#define BC_ACQUIRE             _IOW('b', 5, __u32)
#define BC_RELEASE             _IOW('b', 6, __u32)
#define BC_DECREFS             _IOW('b', 7, __u32)
#define BC_INCREFS_DONE        _IOW('b', 8, struct binder_ptr_cookie)
#define BC_ACQUIRE_DONE        _IOW('b', 9, struct binder_ptr_cookie)
#define BC_REQUEST_DEATH_NOTIFICATION _IOWR('b', 10, struct binder_handle_cookie)
#define BC_DEAD_BINDER_DONE    _IOW('b', 11, binder_uintptr_t)
#define BC_ENTER_LOOPER        _IO('b', 12)
#define BC_EXIT_LOOPER         _IO('b', 13)
#define BC_REGISTER_LOOPER     _IO('b', 15)
#define BC_TRANSACTION_SG      _IOW('b', 16, struct binder_transaction_data_sg)
#define BC_REPLY_SG            _IOW('b', 17, struct binder_transaction_data_sg)

/* Return codes */
#define BR_ERROR               _IOR('r', 0, __s32)
#define BR_OK                  _IO('r', 1)
#define BR_TRANSACTION         _IOR('r', 2, struct binder_transaction_data)
#define BR_REPLY               _IOR('r', 3, struct binder_transaction_data)
#define BR_DEAD_REPLY          _IO('r', 4)
#define BR_TRANSACTION_COMPLETE _IO('r', 5)
#define BR_INCREFS             _IOR('r', 6, struct binder_ptr_cookie)
#define BR_ACQUIRE             _IOR('r', 7, struct binder_ptr_cookie)
#define BR_RELEASE             _IOR('r', 8, struct binder_ptr_cookie)
#define BR_DECREFS             _IOR('r', 9, struct binder_ptr_cookie)
#define BR_ATTEMPT_ACQUIRE     _IOR('r', 10, struct binder_pri_ptr_cookie)
#define BR_DEAD_BINDER         _IOR('r', 11, binder_uintptr_t)
#define BR_SPAWN_LOOPER        _IO('r', 12)
#define BR_FINISHED            _IO('r', 13)
#define BR_NOOP                _IO('r', 14)
#define BR_SOMEONE_DIED        _IO('r', 15)

/* ── Type Definitions ──────────────────────────── */
typedef __UINT64_TYPE__ binder_uintptr_t;
typedef __UINT32_TYPE__ binder_size_t;

/* ── Flat Binder Object ────────────────────────── */
struct flat_binder_object {
    __u32 type;
    __u32 flags;
    union {
        binder_uintptr_t binder;
        __u32 handle;
    };
    binder_uintptr_t cookie;
};

/* Flat binder types */
#define BINDER_TYPE_BINDER      0x73622a85  /* 's' '*' 'b' with magic */
#define BINDER_TYPE_HANDLE      0x73682a85  /* 's' '*' 'h' */
#define BINDER_TYPE_FD          0x66642a85  /* 'f' '*' 'd' */
#define BINDER_TYPE_FDA         0x66646185  /* 'f' 'd' 'a' */
#define BINDER_TYPE_PTR         0x70742a85  /* 'p' '*' 't' */

/* ── Transaction Data ──────────────────────────── */
struct binder_transaction_data {
    union {
        __u32 handle;
        binder_uintptr_t ptr;
    } target;
    binder_uintptr_t cookie;
    __u32 code;
    __u32 flags;
    pid_t sender_pid;
    uid_t sender_euid;
    binder_size_t data_size;
    binder_size_t offsets_size;
    union {
        struct {
            binder_uintptr_t buffer;
            binder_uintptr_t offsets;
        } ptr;
        __u8 buf[8];
    } data;
};

/* Transaction flags */
#define TF_ONE_WAY      0x01    /* Async transaction */
#define TF_ROOT_OBJECT  0x04    /* Root object */
#define TF_STATUS_CODE  0x08    /* Status code */
#define TF_ACCEPT_FDS   0x10    /* Allow file descriptors */

/* ── Binder Version ────────────────────────────── */
struct binder_version {
    __s32 protocol_version;
};

#define BINDER_CURRENT_PROTOCOL_VERSION 8

/* ── Kernel Config Requirements ────────────────── */
/*
 * Required kernel config for SiliconV Android guest:
 *
 * CONFIG_ANDROID=y
 * CONFIG_ANDROID_BINDER_IPC=y
 * CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"
 * CONFIG_ANDROID_BINDER_IPC_SELFTEST=n
 *
 * CONFIG_ASHMEM=y
 *
 * CONFIG_ION=y
 * CONFIG_ION_SYSTEM_HEAP=y
 * CONFIG_ION_CMA_HEAP=y
 *
 * CONFIG_DMA_SHARED_BUFFER=y
 * CONFIG_DMABUF_HEAPS=y
 * CONFIG_DMABUF_HEAPS_SYSTEM=y
 * CONFIG_DMABUF_HEAPS_CMA=y
 */

/* ── SiliconV Binder Device Config ─────────────── */
typedef struct {
    bool enabled;
    int  device_type;    /* BINDER_DEV_* */
    char name[32];       /* "binder", "hwbinder", "vndbinder" */
} sv_binder_config_t;

/* Default SiliconV binder configuration */
static inline sv_binder_config_t sv_binder_config_default(void)
{
    sv_binder_config_t cfg = {
        .enabled = true,
        .device_type = BINDER_DEV_BINDER,
    };
    snprintf(cfg.name, sizeof(cfg.name), "binder");
    return cfg;
}

#endif /* SILICONV_BINDER_H */
