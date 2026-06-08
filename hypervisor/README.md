# hypervisor — Hardware Virtualization

Hypervisor abstraction layer and backend implementations. This is where SiliconV interfaces with the host's hardware virtualization extensions.

## Architecture

```
┌──────────────────────────────────┐
│     SiliconV VM Manager          │
│         (core/vm/)               │
├──────────────────────────────────┤
│     Hypervisor Abstraction       │  ← abstraction/
│     (sv_hv interface)            │
├──────┬──────┬──────┬────────────┤
│ KVM  │ HVF  │ WHPX │  (future) │  ← backends
│Linux │macOS │ Win  │           │
└──────┴──────┴──────┴───────────┘
```

## Backends

| Backend | Platform | Status | Description |
|---------|----------|--------|-------------|
| [kvm/](kvm/) | Linux | ✅ | Linux KVM via `/dev/kvm` |
| [hvf/](hvf/) | macOS | 🔄 | Apple Hypervisor.framework |
| [whpx/](whpx/) | Windows | 🔲 | Windows Hypervisor Platform |

## Abstraction Interface

All backends implement the same interface defined in `abstraction/hv.h`:

```c
typedef struct sv_hv_ops {
    int  (*vm_create)(sv_vm_t *vm);
    int  (*vcpu_create)(sv_vcpu_t *vcpu);
    int  (*vcpu_run)(sv_vcpu_t *vcpu);
    int  (*vcpu_get_reg)(sv_vcpu_t *vcpu, int reg, uint64_t *val);
    int  (*vcpu_set_reg)(sv_vcpu_t *vcpu, int reg, uint64_t val);
    int  (*mmio_read)(sv_vcpu_t *vcpu, uint64_t addr, uint64_t *val);
    int  (*mmio_write)(sv_vcpu_t *vcpu, uint64_t addr, uint64_t val);
    void (*vm_destroy)(sv_vm_t *vm);
} sv_hv_ops_t;
```

## Adding a New Backend

1. Create a directory under `hypervisor/` (e.g., `xen/`)
2. Implement all functions in `sv_hv_ops_t`
3. Register the backend in `abstraction/hv.c`
4. Add build configuration in `CMakeLists.txt`
