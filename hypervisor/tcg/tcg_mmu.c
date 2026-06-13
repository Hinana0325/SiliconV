/*
 * SiliconV — TCG MMU (Memory Management Unit)
 *
 * Handles guest memory access resolution:
 *   - RAM access → direct pointer into vm->ram
 *   - MMIO access → exit to machine.c MMIO dispatch
 *
 * The guest physical address space layout (from MMIO spec):
 *   0x00000000 - 0x03ffffff : NOR flash (QEMU virt)
 *   0x08000000 - 0x0800ffff : GICv3 Distributor
 *   0x080a0000 - 0x080bffff : GICv3 Redistributor
 *   0x09000000 - 0x09000fff : PL011 UART
 *   0x0a000000 - 0x0a00ffff : virtio MMIO
 *   0x10000000 - 0x3efeffff : PCIe MMIO
 *   0x40000000 - ...        : DRAM (config.ram_base, default 16GB)
 */

#include "tcg.h"
#include "tcg_cpu.h"
#include "tcg_mmu.h"

#include <stdio.h>
#include <string.h>

/* ── MMIO address ranges ───────────────────────────────── */
static inline bool is_mmio(uint64_t addr, tcg_vm_t *vm)
{
    uint64_t ram_base = vm->config.ram_base;
    uint64_t ram_end  = ram_base + vm->ram_size;

    /* RAM range: direct access */
    if (addr >= ram_base && addr + 8 <= ram_end) return false;

    /* Everything below RAM base is MMIO (devices) */
    if (addr < ram_base) return true;

    /* Above RAM is MMIO */
    return true;
}

/* ── MMIO Access ────────────────────────────────────────── */
/* These functions handle MMIO reads/writes through the
 * machine.c callback mechanism */

static uint64_t do_mmio_read(tcg_vcpu_t *vcpu, uint64_t addr, int size)
{
    tcg_vm_t *vm = vcpu->vm;
    if (!vm->mmio_read) return 0;

    uint64_t data = vm->mmio_read(vm->callback_opaque, addr, size);

    /* UART output capture for debugging */
    if (addr >= 0x09000000 && addr < 0x09001000) {
        /* Reads from UART are rare but legitimate (status regs) */
    }

    return data;
}

static void do_mmio_write(tcg_vcpu_t *vcpu, uint64_t addr, uint64_t value, int size)
{
    tcg_vm_t *vm = vcpu->vm;
    if (!vm->mmio_write) return;

    vm->mmio_write(vm->callback_opaque, addr, value, size);

    /* UART output capture for debugging */
    if (addr >= 0x09000000 && addr < 0x09001000) {
        /* machine_uart_tx will call putchar() */
    }
}

/* ── Public: MMU Resolve ────────────────────────────────── */
/* Returns:
 *   0 = RAM access completed (val populated for reads)
 *   1 = MMIO access (called into machine.c, val populated for reads)
 *  -1 = error
 */
int tcg_mmu_resolve(tcg_vcpu_t *vcpu, uint64_t addr, int size,
                     uint64_t *val, bool is_write)
{
    tcg_vm_t *vm = vcpu->vm;

    if (is_mmio(addr, vm)) {
        if (is_write) {
            do_mmio_write(vcpu, addr, *val, size);
        } else {
            *val = do_mmio_read(vcpu, addr, size);
        }
        return 1; /* MMIO — caller should handle exit if needed */
    }

    /* RAM access */
    uint64_t offset = addr - vm->config.ram_base;

    if (is_write) {
        switch (size) {
        case 1: vm->ram[offset] = (uint8_t)*val; break;
        case 2: *(uint16_t *)(vm->ram + offset) = (uint16_t)*val; break;
        case 4: *(uint32_t *)(vm->ram + offset) = (uint32_t)*val; break;
        case 8: *(uint64_t *)(vm->ram + offset) = *val; break;
        default: return -1;
        }
    } else {
        switch (size) {
        case 1: *val = vm->ram[offset]; break;
        case 2: *val = *(uint16_t *)(vm->ram + offset); break;
        case 4: *val = *(uint32_t *)(vm->ram + offset); break;
        case 8: *val = *(uint64_t *)(vm->ram + offset); break;
        default: return -1;
        }
    }

    return 0; /* RAM access */
}

/* ── Public: MMU Read (instruction fetch) ───────────────── */
int tcg_mmu_read(tcg_vm_t *vm, uint64_t addr, void *buf, int size)
{
    uint64_t offset = addr - vm->config.ram_base;

    if (offset + size > vm->ram_size) {
        /* Instruction fetch from MMIO space — unsupported */
        fprintf(stderr, "tcg: instruction fetch from non-RAM at 0x%lx\n",
                (unsigned long)addr);
        return -1;
    }

    memcpy(buf, vm->ram + offset, size);
    return 0;
}
