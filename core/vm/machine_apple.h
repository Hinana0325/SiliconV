/*
 * SiliconV — Apple Machine Profile
 *
 * Apple-specific VM machine state and initialization.
 * Parallel to the Android machine (machine.h/c) but using
 * Apple device models instead:
 *   - Apple AIC (instead of GICv3)
 *   - Apple S5L UART (instead of PL011)
 *   - Apple DART (IOMMU)
 *   - Apple SEP (Secure Enclave)
 *   - Apple WDT, NVRAM
 *   - XNU boot loader (instead of bootimg)
 *
 * Both profiles coexist in the same binary, selected at runtime.
 */

#ifndef SILICONV_MACHINE_APPLE_H
#define SILICONV_MACHINE_APPLE_H

#include <stdint.h>
#include <stdbool.h>

#include "../../devices/apple-aic/apple_aic.h"
#include "../../devices/apple-uart/apple_uart.h"
#include "../../devices/apple-dart/apple_dart.h"
#include "../../devices/apple-sep/apple_sep.h"
#include "../../devices/apple-wdt/apple_wdt.h"
#include "../../devices/apple-nvram/apple_nvram.h"
#include "../../devices/apple-timer/apple_timer.h"
#include "../../devices/apple-gpio/apple_gpio.h"
#include "../../devices/apple-i2c/apple_i2c.h"
#include "../../devices/apple-spmi/apple_spmi.h"
#include "../../devices/apple-nvme/apple_nvme.h"
#include "../boot/apple/xnu_boot.h"
#include "../memory/dtb.h"
#include "../object/psci.h"
#include "../../devices/transport/virtio_mmio.h"
#include "../../devices/virtio-blk/virtio_blk.h"
#include "../../devices/virtio-net/virtio_net.h"
#include "../../devices/virtio-console/virtio_console.h"
#include "../../hypervisor/abstraction/hv.h"

/* ── Apple Machine State ────────────────────────── */
typedef struct {
    /* Core Apple components */
    apple_aic_state_t   aic;            /* Interrupt controller (replaces GIC) */
    apple_uart_state_t  uart;           /* Serial console (replaces PL011) */
    apple_dart_state_t  dart[2];        /* IOMMU (2 instances) */
    apple_sep_state_t   sep;            /* Secure Enclave */
    apple_wdt_state_t   wdt;            /* Watchdog timer */
    apple_nvram_state_t nvram;          /* NVRAM storage */
    apple_timer_state_t timer;          /* System timer/counter */
    apple_gpio_state_t  gpio;           /* General-purpose I/O */
    apple_i2c_state_t   i2c;            /* I2C bus controller */
    apple_spmi_state_t  spmi;           /* System Power Management Interface */
    apple_nvme_state_t  nvme;           /* NVMe Storage Controller */

    /* PSCI (shared with Android profile) */
    psci_state_t        psci;

    /* Virtio devices (shared with Android profile) */
    virtio_blk_t        virtio_blk;
    bool                virtio_blk_enabled;
    virtio_net_t        virtio_net;
    bool                virtio_net_enabled;
    virtio_console_t    virtio_console;
    bool                virtio_console_enabled;

    /* XNU boot state */
    xnu_boot_state_t    xnu_boot;

    /* Guest memory */
    uint8_t            *ram;
    uint64_t            ram_base;
    uint64_t            ram_size;

    /* Kernel info */
    uint64_t            kernel_entry;

    /* VM config */
    int                 num_cpus;
    bool                running;
} sv_machine_apple_t;

/* ── API ───────────────────────────────────────── */

/* Initialize Apple VM */
int sv_machine_apple_init(sv_machine_apple_t *vm, int num_cpus, uint64_t ram_size);

/* Destroy Apple VM */
void sv_machine_apple_destroy(sv_machine_apple_t *vm);

/* Load XNU kernel (IMG4 or raw Mach-O) */
int sv_machine_apple_load_kernel(sv_machine_apple_t *vm,
                                  const uint8_t *data, size_t size,
                                  const char *cmdline);

/* Load DTB (optional, overrides generated one) */
int sv_machine_apple_load_dtb(sv_machine_apple_t *vm,
                               const uint8_t *data, size_t size);

/* Attach virtio devices */
int sv_machine_apple_attach_virtio_blk(sv_machine_apple_t *vm,
                                        const char *image_path, bool read_only);
int sv_machine_apple_attach_virtio_net(sv_machine_apple_t *vm);
int sv_machine_apple_attach_virtio_console(sv_machine_apple_t *vm);

/* Attach NVMe storage (Apple native) */
int sv_machine_apple_attach_nvme(sv_machine_apple_t *vm,
                                  const char *image_path, bool read_only);

/* Run the VM */
int sv_machine_apple_run(sv_machine_apple_t *vm);

/* Stop the VM */
void sv_machine_apple_stop(sv_machine_apple_t *vm);

/* ── MMIO Dispatch (called by hypervisor backend) ── */
uint64_t sv_mmio_apple_read(sv_machine_apple_t *vm, uint64_t addr, int size);
void     sv_mmio_apple_write(sv_machine_apple_t *vm, uint64_t addr,
                              uint64_t value, int size);

#endif /* SILICONV_MACHINE_APPLE_H */
