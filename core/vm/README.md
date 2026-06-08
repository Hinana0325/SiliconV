# core/vm — VM Machine

The main virtual machine manager. Creates the VM, loads the kernel, registers all devices, and runs the vCPU event loop.

## Files

| File | Description |
|------|-------------|
| `machine.c` / `machine.h` | Main VM lifecycle — create, configure, run, destroy |
| `bootimg.c` / `bootimg.h` | Android boot.img parser (v0–v4) |
| `boot_stub.S` | Assembly boot stub — initial guest entry point |
| `boot_stub.ld` | Linker script for the boot stub |

## Boot Flow

```
1. Parse boot.img → extract kernel, ramdisk, cmdline, DTB
2. Create VM (hypervisor backend)
3. Load kernel at entry address
4. Generate DTB from SiliconV spec
5. Register devices (GIC, UART, virtio-blk, etc.)
6. Load ramdisk into guest memory
7. Set initial CPU state (PC → boot_stub)
8. Run vCPUs
9. Handle MMIO exits → dispatch to devices
```

## Boot Image Support

Supports Android boot image versions 0 through 4:
- **v0/v1**: Original format with kernel + ramdisk
- **v2/v3**: Added recovery DTB, boot signature
- **v4**: Vendor boot image partition (GKI)

## API

```c
/* Parse a boot.img file */
sv_bootimg_t *sv_bootimg_parse(const char *path);

/* Create and run the VM */
sv_vm_t *sv_vm_create(const sv_vm_config_t *config);
int sv_vm_run(sv_vm_t *vm);
void sv_vm_destroy(sv_vm_t *vm);
```
