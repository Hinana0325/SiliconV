# frontend/cli — Command-Line Interface

The primary SiliconV frontend. Minimal, scriptable, and works on all platforms.

## Usage

```bash
./siliconv [options]

Options:
  -k, --kernel <path>     Kernel image path (required)
  -r, --rootfs <path>     Root filesystem image (required)
  -c, --cpus <n>          Number of vCPUs (default: 4)
  -m, --memory <size>     Guest RAM (default: 4G)
  -s, --serial <path>     Serial output to file (default: stdout)
  -d, --disk <path>       Additional virtio-blk disk
  -g, --gpu               Enable virtio-gpu
  -n, --net               Enable virtio-net
  -v, --verbose           Verbose logging
  -h, --help              Show this help
```

## Examples

```bash
# Basic boot
./siliconv -k Image -r rootfs.img

# Custom config
./siliconv -k Image -r rootfs.img -c 8 -m 8G -g -n

# Serial to file
./siliconv -k Image -r rootfs.img -s serial.log
```

## Files

| File | Description |
|------|-------------|
| `main.c` | CLI entry point, argument parsing, VM launch |

## Keyboard Shortcuts (when running)

| Key | Action |
|-----|--------|
| `Ctrl-A X` | Exit |
| `Ctrl-A C` | Serial console monitor |
