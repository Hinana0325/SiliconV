# scripts — Build & Test Scripts

Utility scripts for building, testing, and debugging SiliconV.

## Scripts

| Script | Description |
|--------|-------------|
| `build_kernel.sh` | Build Android Common Kernel for SiliconV (Linux) |
| `build_kernel_macos.sh` | Build Android Common Kernel on macOS (cross-compile) |
| `test_qemu.sh` | Test SiliconV kernel boot using QEMU (validation) |

## build_kernel.sh

Builds the Android Common Kernel with SiliconV configuration:

```bash
./scripts/build_kernel.sh android14-6.6

# Options:
#   <branch>     Kernel branch to build (default: android14-6.6)
#   --clean      Clean build (remove previous build)
#   --menuconfig Open kernel menuconfig before building
```

## build_kernel_macos.sh

Cross-compile the ARM64 kernel on macOS:

```bash
./scripts/build_kernel_macos.sh android14-6.6

# Requires:
#   brew install aarch64-elf-gcc aarch64-elf-binutils
```

## test_qemu.sh

Test boot the kernel using QEMU for quick validation without running the full SiliconV hypervisor:

```bash
./scripts/test_qemu.sh

# Useful for:
#   - Verifying kernel boots correctly
#   - Testing DTB compatibility
#   - Debugging early boot issues
```

## Adding Scripts

Place new scripts here and make them executable:

```bash
chmod +x scripts/my_script.sh
```

Document the script's purpose and usage in this README.
