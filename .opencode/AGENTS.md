# AGENTS.md — SiliconV

Virtual ARM64 phone hypervisor platform written in **C11** (no C++). Runs unmodified
Linux/Android kernels via KVM (Linux ARM64) or HVF (macOS Apple Silicon).
This is a hypervisor — not an emulator.

## Build & Run

```bash
# Host build (always works on x86_64, ARM64, macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Dry-run validation (works on any host, no ARM64 needed)
./build/sv-cli --dry-run -k Image -r rootfs.img -m 1024 -n 2

# Run on ARM64 host
./build/sv-cli -k Image -r rootfs.img -m 4096 -n 4
```

**Two CMake targets:** `sv-cli` (CLI, always) and `SiliconV.app` (Cocoa macOS bundle, `APPLE` only).

The old target name `siliconv` was renamed to `sv-cli` to fix a case-insensitive macOS
filesystem collision with the `SiliconV.app` bundle directory. CI artifacts still call it
`siliconv` — the CI uses `cmake --build build` and the compiled binary is `build/sv-cli`.

## Test

```bash
# Build with tests enabled, then run
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSV_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Unit tests only (3 exist: DTB, UART, virtio-mmio)
ctest --test-dir build -L unit
```

No test framework — tests are standalone `.c` files with a manual `CHECK()` macro.
Integration tests exist only as stubs (`.gitkeep` files).

## Cross-compile Dependencies

| What | Package | Needed for |
|------|---------|------------|
| ARM64 assembler/linker | `gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu` | `boot_stub.S` boot stub |
| ARM64 cross-compiler | `gcc-aarch64-linux-gnu` | `build_kernel.sh` + kernel CI |
| QEMU ARM64 | `qemu-system-arm` | `test_qemu.sh` integration test |

CMake auto-detects the cross-compiler. Without it, `boot_stub.bin` is skipped with a
status message (not a hard error). The boot stub is a small ELF binary assembled from
`core/vm/boot_stub.S`, linked via `core/vm/boot_stub.ld`, and objcopy'd to flat binary.

## Project Layout

```
spec/            ← Frozen hardware spec (SVABI v0 — change spec/ BEFORE code)
core/vm/         ← Main loop (machine.c), boot stub, bootimg parser ← ENTRYPOINT
  machine.c      ← sv_machine_init() → sv_machine_run() ties everything together
  boot_stub.S    ← ARM64 assembly: "Hello SiliconV" via PL011 UART
core/irq/        ← GICv3 emulation
core/object/     ← PSCI (CPU lifecycle)
core/memory/     ← DTB generator (runtime, auto-generated if no DTB provided)
devices/         ← PL011 UART, virtio-blk, virtio-net, virtio-console, virtio MMIO transport
hypervisor/      ← Abstraction layer + KVM backend (ARM64 Linux) + HVF (macOS)
  abstraction/   ← Backend registry: prefers KVM > HVF > WHPX
  kvm/           ← /dev/kvm API, registered at library init via __attribute__((constructor))
  hvf/           ← Hypervisor.framework (Apple Silicon)
frontend/cli/    ← CLI launcher (main.c)
scripts/         ← build_kernel.sh (clones AOSP common), test_qemu.sh (QEMU integration)
kernel/configs/  ← android.config fragment (merge with GKI defconfig)
android/         ← AOSP integration (binder, graphics, init, shims, sepolicy)
tests/           ← unit/ (3 C files), integration/ (empty), performance/ (empty)
```

## Architecture

**Entrypoint:** `frontend/cli/main.c` → `sv_machine_init()` → `sv_machine_load_kernel()`
→ `sv_machine_generate_dtb()` / `sv_machine_load_dtb()` → `sv_machine_run()`.

**MMIO map (from spec):**
| Address Range | Device |
|--------------|--------|
| `0x08000000 – 0x0800FFFF` | GICv3 Distributor |
| `0x08010000 + cpu*0x20000` | GICv3 Redistributor (per-CPU) |
| `0x10000000 – 0x1000FFFF` | PL011 UART |
| `0x20000000 – 0x2000FFFF` | virtio-blk |
| `0x20010000 – 0x2001FFFF` | virtio-net |
| `0x20040000 – 0x2004FFFF` | virtio-console |

MMIO dispatch lives in `core/vm/machine.c` (`sv_mmio_read`/`sv_mmio_write`).
The KVM backend calls these directly from `KVM_EXIT_MMIO` handling — no separate
dispatch step in the main loop.

**Hypervisor backends** auto-detected at build time (CMake `#ifdef`):
- ARM64 Linux → KVM (`/dev/kvm` API, GICv3 via `KVM_CREATE_DEVICE`)
- Apple Silicon macOS → HVF (`Hypervisor.framework`)
- Everything else → placeholder (prints config, no VM execution)

Backend registration uses `__attribute__((constructor))` — not `main()`.

**Boot:** Kernel loaded at `ram_base + 32MB` (0x402000000). DTB at `ram_base + 2MB`
(0x400200000). vCPU init: x0 = DTB address, x1 = CPU ID, PC = kernel_entry,
CPSR = EL1h with masked DAIF. Supports both raw kernel binaries and Android boot.img
(v0–v4, auto-detect).

## CI (GitHub Actions)

| Workflow | Trigger | Notes |
|----------|---------|-------|
| `ci.yml` | Push/PR to main/master | Build x86_64 host, cross-compile boot stub, spec validation, clang-tidy (optional) |
| `kernel.yml` | Manual dispatch or push to `kernel/configs/**` | Clones AOSP common kernel 6.6, merges `android.config`, builds Image + modules |
| `release.yml` | Tag push `v*` | Builds x86_64 + ARM64 Linux tarballs, creates GitHub release |

**CI runs only on Linux** — no macOS runner. HVF backend is tested manually.

## Conventions

- **Spec before code** — any change touching hardware interface updates `spec/` first
- **AOSP first** — vendor ROMs are out of scope until AOSP is stable
- **Reuse, don't rewrite** — minigbm, drm_hwcomposer, Mesa, virglrenderer
- **VirGL before Venus** — 3D via VirGL first, Vulkan later
- **C11 strict** (`-std=c11 -Wall -Wextra -Wpedantic`)
- **Static libs only** — no shared libraries in the build system
- **UART is your lifeline** — guest console output is the primary debug channel
- Current development phase: **Phase 3 (Android Kernel)** per `ROADMAP.md`

## OpenCode Commands

Defined in `opencode.yml`:
- `opencode build` — cmake build pipeline
- `opencode spec-review` — cross-check spec consistency (architect agent, no write/edit)
- `opencode kernel-check` — review `kernel/configs/android.config` (kernel agent)
