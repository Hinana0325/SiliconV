# hypervisor/abstraction — Hypervisor Abstraction Layer

Defines the common interface that all hypervisor backends must implement.

## Purpose

SiliconV supports multiple hypervisor backends (KVM, HVF, WHPX). The abstraction layer ensures the VM manager (`core/vm/`) doesn't depend on any specific backend.

## Files

| File | Description |
|------|-------------|
| `hv.c` / `hv.h` | Backend interface definition, factory function, common utilities |

## Interface

```c
/* Select and initialize a backend */
sv_hv_type_t sv_hv_detect(void);           // auto-detect best backend
sv_hv_ops_t *sv_hv_get_ops(sv_hv_type_t);  // get ops for backend

/* VM lifecycle */
int  sv_hv_vm_create(sv_vm_t *vm);
void sv_hv_vm_destroy(sv_vm_t *vm);

/* vCPU lifecycle */
int sv_hv_vcpu_create(sv_vcpu_t *vcpu);
int sv_hv_vcpu_run(sv_vcpu_t *vcpu);
int sv_hv_vcpu_get_reg(sv_vcpu_t *vcpu, int reg, uint64_t *val);
int sv_hv_vcpu_set_reg(sv_vcpu_t *vcpu, int reg, uint64_t val);

/* Memory mapping */
int sv_hv_mem_map(sv_vm_t *vm, uint64_t guest_addr, void *host_addr,
                  size_t size, int prot);
```

## Backend Selection

At startup, SiliconV auto-detects the available backend:

1. Try KVM (`/dev/kvm`) — Linux
2. Try HVF — macOS
3. Try WHPX — Windows
4. Fail with error if none available
