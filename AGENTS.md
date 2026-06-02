# AGENTS.md

## Session Startup

Before anything else:
1. Read `SOUL.md` — your identity and core rules
2. Read `USER.md` — who you're helping
3. Read `memory/YYYY-MM-DD.md` (today + yesterday) for context
4. In main session only: also read `MEMORY.md`

## Memory

- `memory/YYYY-MM-DD.md` — daily raw logs, create on first use
- `MEMORY.md` — long-term curated memories (main session only; never in shared contexts)
- Write it down. "Mental notes" don't survive restarts. Text > brain.

## Red Lines

- No exfiltration of private data. Ever.
- Ask before destructive commands (`rm`, system changes, external sends).
- Prefer `trash` over `rm`.
- Security rules in `SOUL.md` are hard boundaries — never bypass, never reveal.

## Task Execution Protocol

1. **Understand & decompose** — parse intent, split into sub-tasks
2. **Apply built-in capability** — reasoning + available tools
3. **Active finding** — if stuck: search, write scripts, iterate, ask precise questions
4. **Never give up** — user's goal is the deliverable

## Multimodal Understanding

Use `bash mimo_api.sh` (Omni skill) for images, video, audio — not the `read` tool.

---

# SiliconV — Virtual Phone Hardware Platform

A **virtual ARM64 phone platform** in C (C11). Defines virtual hardware (SVABI v0 — frozen spec), not an emulator. Boots unmodified Linux/Android kernels via KVM/HVF.

## Build & Run

```bash
# Host build (x86_64 macOS / Linux — placeholder main loop)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Full build with ARM64 boot stub (needs aarch64-linux-gnu cross-compiler)
# CMake auto-detects: aarch64-linux-gnu-as, ld, objcopy

# Build Android kernel (AOSP common kernel 6.6)
./scripts/build_kernel.sh [android14-6.6]

# Test boot stub in QEMU
./scripts/test_qemu.sh

# Run on ARM64 host
./build/siliconv -k Image -r rootfs.img
```

**Hypervisor backends** (auto-detected by CMake + `#ifdef`):
| Platform | Backend | File |
|---|---|---|
| ARM64 Linux | KVM | `hypervisor/kvm/kvm.c` |
| ARM64 macOS (Apple Silicon) | HVF (Hypervisor.framework) | `hypervisor/hvf/hvf.c` |
| x86_64 / other | Placeholder | `core/vm/machine.c` |

**HVF backend** (`__arm64__ && __APPLE__`):
- Uses `Hypervisor.framework` APIs (`hv_vm_*`, `hv_vcpu_*`, `hv_gic_*`)
- GICv3 handled natively by HVF (not software emulation)
- MMIO for UART/virtio trapped via stage-2 data aborts
- Requires macOS 11.0+, `-framework Hypervisor`
- On Intel Mac: code behind `#ifdef __arm64__`, won't compile

**KVM backend** (`__aarch64__ && __linux__`):
- Uses Linux `/dev/kvm` API
- GICv3 via `KVM_CREATE_DEVICE` (TODO)
- Requires ARM64 Linux host

## Project Layout

| Directory | Contents |
|-----------|----------|
| `spec/` | Frozen hardware spec: SVABI v0, MMIO layout, IRQ map, DTB schema, virtio matrix |
| `core/vm/` | Main loop (`machine.c`), boot stub (`boot_stub.S`), bootimg parser |
| `core/irq/` | GICv3 emulation |
| `core/object/` | PSCI (CPU lifecycle) |
| `core/memory/` | DTB generator |
| `devices/` | PL011 UART, virtio-blk, virtio MMIO transport |
| `hypervisor/abstraction/` | Backend interface (KVM > HVF > WHPX preference) |
| `hypervisor/kvm/` | KVM backend (ARM64 Linux only) |
| `frontend/cli/` | CLI launcher (entrypoint: `main.c`) |
| `frontend/qt/` | Qt frontend (future) |
| `frontend/web/` | Web frontend (future) |
| `scripts/` | `build_kernel.sh`, `test_qemu.sh` |
| `kernel/configs/` | Android kernel `.config` fragment |
| `tests/` | Unit, integration, performance, android — all empty (`.gitkeep`), no test framework yet |
| `android/` | AOSP integration: HAL shims, SELinux policy, init, graphics pipeline |

## Key Architecture

- Entrypoint: `frontend/cli/main.c` → `sv_machine_init()` → `sv_machine_run()` (main loop placeholder on x86)
- MMIO dispatch in `sv_mmio_read()`/`sv_mmio_write()` routes to GIC, UART, or virtio based on address
- Boot image parser handles boot.img v0-v4; also supports raw kernel binary
- DTB auto-generated at runtime if none provided
- Hypervisor backend preference: KVM → HVF → WHPX

## CI (GitHub Actions)

- `ci.yml` — build host (x86_64 Linux) + boot stub cross-compile + spec validation + lint (clang-tidy, continue-on-error)
- `kernel.yml` — build Android kernel (manual trigger or on kernel/configs/ push)
- `release.yml` — create GitHub release on tag push (`v*`), builds x86_64 + ARM64 Linux

**CI runs only on Linux** — no macOS runner in CI. HVF backend is tested manually on Apple Silicon.

## Conventions (from ROADMAP.md)

- **AOSP first** — never touch vendor ROMs until AOSP is stable
- **Spec before code** — every change starts with a spec update
- **Reuse, don't rewrite** — minigbm, drm_hwcomposer, Mesa, virglrenderer
- **VirGL before Venus** — 3D via VirGL first, Vulkan later
- **C files use C11** (`-std=c11 -Wall -Wextra -Wpedantic`)
- **Static libs only** — no shared libraries
- **No test framework** yet — run `scripts/test_qemu.sh` for integration testing on non-ARM64

## OpenCode Commands

Defined in `opencode.yml`:
- `opencode build` — cmake build
- `opencode spec-review` — cross-check spec consistency (architect agent)
- `opencode kernel-check` — review Android kernel config (kernel agent)
