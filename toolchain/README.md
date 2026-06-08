# toolchain — Build Tools

Utility tools for building, patching, and packaging SiliconV images.

## Tools

| Tool | Description | Status |
|------|-------------|--------|
| [dtb/](dtb/) | Device tree blob manipulation tools | 🔲 |
| [kernel_replace/](kernel_replace/) | Kernel image replacement in boot.img | 🔲 |
| [repack/](repack/) | Repack boot.img with modified components | 🔲 |
| [unpack/](unpack/) | Unpack boot.img into kernel, ramdisk, DTB | 🔲 |
| [shim_generator/](shim_generator/) | Auto-generate HAL shim code from headers | 🔲 |
| [vendor_patch/](vendor_patch/) | Apply vendor-specific patches to AOSP | 🔲 |

## Workflow

```
Original boot.img
    ↓ unpack/
┌───────────────┐
│ kernel (Image) │
│ ramdisk.img    │
│ dtb (optional) │
│ cmdline        │
└───────┬───────┘
    ↓ modify
    ↓ repack/
Modified boot.img
```

## Usage

```bash
# Unpack a boot.img
./toolchain/unpack/unpack boot.img -o extracted/

# Repack with a new kernel
./toolchain/repack/repack extracted/ -k new_Image -o new_boot.img

# Replace kernel in existing boot.img
./toolchain/kernel_replace/replace boot.img new_Image
```
