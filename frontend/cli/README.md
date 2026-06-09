# frontend/cli — Command-Line Interface

The primary SiliconV frontend. Minimal, scriptable, and works on all platforms.

## Usage

```bash
./build/sv-cli [OPTIONS]

Options:
  -k, --kernel PATH    Kernel image or Android boot.img (required)
  -d, --dtb PATH       Device tree blob (optional, generated if omitted)
  -r, --rootfs PATH    Root filesystem image for virtio-blk
  -c, --cmdline STR    Kernel command line
  -m, --memory SIZE    Guest RAM in MB (default: 4096, minimum: 64)
  -n, --cpus NUM       Number of vCPUs (default: 4, max: 8)
      --dry-run        Load and validate configuration without starting vCPUs
  -h, --help           Show this help
```

## Examples

```bash
# Validate a kernel/rootfs configuration without starting vCPUs
./build/sv-cli --dry-run -k Image -r rootfs.img -m 1024 -n 2

# Basic ARM64 host boot
./build/sv-cli -k Image -r rootfs.img

# Custom command line
./build/sv-cli -k Image -r rootfs.img \
  -c "console=ttyAMA0 earlycon=pl011,0x10000000 root=/dev/vda rw"
```

On x86_64 and other hosts without a SiliconV hypervisor backend, `--dry-run` is the recommended smoke test because it still verifies argument parsing, image loading, DTB generation, and device setup.

## Files

| File | Description |
|------|-------------|
| `main.c` | CLI entry point, argument parsing, VM launch |
