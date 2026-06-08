/*
 * SiliconV — VM Machine (Main Loop)
 *
 * Ties together all components:
 *   - GICv3 interrupt controller
 *   - PL011 UART
 *   - Virtio devices (block, net, gpu, input, console)
 *   - PSCI for CPU lifecycle
 *   - DTB generator
 *
 * This is the hypervisor's main loop: create VM, load kernel,
 * register devices, run vCPUs, handle MMIO exits.
 */

#ifndef SILICONV_MACHINE_H
#define SILICONV_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
#include "../irq/gic.h"
#include "../memory/dtb.h"
#include "../object/psci.h"
#include "../../devices/uart/pl011.h"
#include "../../devices/transport/virtio_mmio.h"
#include "../../devices/virtio-blk/virtio_blk.h"
#include "../../devices/virtio-net/virtio_net.h"
#include "../../devices/virtio-console/virtio_console.h"
#include "../../hypervisor/abstraction/hv.h"

/* ── VM Machine State ──────────────────────────── */
typedef struct {
    /* Core components */
    gic_state_t     gic;
    psci_state_t    psci;
    pl011_state_t   uart;
    dtb_config_t    dtb_config;

    /* Virtio devices */
    virtio_blk_t    virtio_blk;
    bool            virtio_blk_enabled;
    virtio_net_t    virtio_net;
    bool            virtio_net_enabled;
    virtio_console_t virtio_console;
    bool            virtio_console_enabled;

    /* Guest memory */
    uint8_t        *ram;
    uint64_t        ram_base;
    uint64_t        ram_size;

    /* Kernel/DTB info */
    uint64_t        kernel_entry;
    uint64_t        dtb_addr;

    /* VM config */
    int             num_cpus;
    bool            running;
} sv_machine_t;

/* ── API ───────────────────────────────────────── */

/* Initialize the virtual machine */
int sv_machine_init(sv_machine_t *vm, int num_cpus, uint64_t ram_size);

/* Destroy the virtual machine */
void sv_machine_destroy(sv_machine_t *vm);

/* Load a kernel image (raw binary or Android boot.img) */
int sv_machine_load_kernel(sv_machine_t *vm, const char *path);

/* Load a DTB (or generate one) */
int sv_machine_load_dtb(sv_machine_t *vm, const char *path);

/* Attach a virtio-blk device with a disk image */
int sv_machine_attach_virtio_blk(sv_machine_t *vm, const char *image_path,
                                  bool read_only);

/* Attach a virtio-net device */
int sv_machine_attach_virtio_net(sv_machine_t *vm);

/* Attach a virtio-console device */
int sv_machine_attach_virtio_console(sv_machine_t *vm);

/* Generate and load the DTB */
int sv_machine_generate_dtb(sv_machine_t *vm);

/* Start the VM (enters main loop) */
int sv_machine_run(sv_machine_t *vm);

/* Stop the VM */
void sv_machine_stop(sv_machine_t *vm);

/* ── MMIO Dispatch ─────────────────────────────── */
/* Called by hypervisor backend when guest accesses MMIO */
typedef struct {
    sv_machine_t *machine;
} sv_mmio_dispatch_t;

/* Handle an MMIO read from the guest */
uint64_t sv_mmio_read(sv_machine_t *vm, uint64_t addr, int size);

/* Handle an MMIO write from the guest */
void sv_mmio_write(sv_machine_t *vm, uint64_t addr, uint64_t value, int size);

#endif /* SILICONV_MACHINE_H */
