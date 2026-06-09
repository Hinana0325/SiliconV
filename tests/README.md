# tests — Test Suite

Automated tests for SiliconV components.

## Test Categories

| Category | Description | Status |
|----------|-------------|--------|
| [unit/](unit/) | Unit tests for individual components | ✅ |
| [integration/](integration/) | Integration tests (full VM boot) | 🔲 |
| [performance/](performance/) | Performance benchmarks | 🔲 |
| [android/](android/) | Android-specific tests (boot, graphics, IPC) | 🔲 |

## Running Tests

```bash
# Configure with tests enabled
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSV_BUILD_TESTS=ON

# Build and run all tests
cmake --build build
ctest --test-dir build --output-on-failure

# Unit tests only
ctest --test-dir build -L unit

# Integration tests (requires ARM64 or QEMU)
ctest --test-dir build -L integration
```

## Test Matrix

| Test | Type | Description |
|------|------|-------------|
| `test_dtb` | Unit | Generated FDT header and expected SiliconV nodes |
| `test_uart` | Unit | PL011 TX callback and RX FIFO behavior |
| `test_virtio_mmio` | Unit | Virtio-MMIO identity registers, avail ring layout, and interrupt ack |
| `test_full_boot` | Integration | Kernel boots to shell |
| `test_android_init` | Integration | Android init completes |
| `test_gpu_render` | Integration | VirGL renders a frame |
| `test_boot_time` | Perf | Time to first frame |
| `test_block_io` | Perf | Disk I/O throughput |

## Adding Tests

1. Place test files in the appropriate category directory
2. Register in `CMakeLists.txt` with `add_test()`
3. Add labels for filtering (`unit`, `integration`, `performance`)
